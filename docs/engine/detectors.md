# Detectors

The detector library (`engine/include/isrt/detector.hpp`) is plain C++17 with
no operating-system calls. The caller passes monotonic time in nanoseconds:
on QNX it comes from the timer pulse and the arrival stamp, on Linux from
`clock_gettime(CLOCK_MONOTONIC)`, in tests from a fake clock. Every node has a
fixed-size state record; the library allocates nothing after construction.

## Inputs

| Call | When |
|---|---|
| `on_message(node, boot, seq, now)` | a message with a valid signature arrived |
| `on_tick(now)` | a timer fired; the OS layer keeps one timer armed to `next_tick()` |

## Rules

| Alert | Severity | Rule |
|---|---|---|
| `missing` | critical | no in-order message for `miss_periods` periods (default 5) after the last one; fires once, at the deadline |
| `recovered` | info | a missing node sends again, a degrading node has `degrade_streak` normal intervals, or a storm falls to half the limit (at least 1); `cleared` says which |
| `degrading` | warning | after `warmup_samples` intervals (default 20), `degrade_streak` (default 3) intervals in a row above `mean + max(k * deviation, period / 2)`, k = 4; fires once until it recovers |
| `storm` | warning | more than `rate_max` messages in `rate_window` (default: 5 times the expected rate in 1 s, at least 5); counted in 10 buckets |
| `duplicate` | warning | the same sequence number again within one boot; once per run, until the next in-order message |
| `reordered` | warning | a stale message: a lower sequence number, or a lower boot counter; once per run, until the next in-order message |
| `rebooted` | info | a higher boot counter; the sequence restarts and the gap is not a sample |

## Boot counter and sequence numbers

The boot id is a counter the node keeps in flash and raises on every start. A
higher counter is a restart; a lower one is an old message replayed and is
treated as stale. Messages are signed, so only the node can raise its counter.
Sequence numbers are compared as serial numbers, so the wrap from 4294967295
to 0 is forward. A forward step is accepted only up to what the storm limit
allows for the time since the last message (one rate window more than has
passed); a bigger jump is an old message from far back in a long boot and is
stale. After a long outage the allowance grows with the outage, so a node
whose messages were lost meanwhile is accepted. The first message after the
engine starts has nothing to compare against and is accepted as it is.

## Liveness and the baseline

Every in-order message moves the deadline. A duplicate or stale message does
not: a replayed message is not proof that the node is alive. A timestamp
earlier than the last arrival moves nothing.

Mean and deviation are exponentially weighted (alpha 1/16) and are updated
only by normal intervals, so a slow streak does not teach the baseline to
accept slowness. Intervals shorter than a quarter of the period are flood
traffic: they keep the node alive and count toward the storm rate, but they
are not samples. If a node's real period changes, `degrading` fires once and
stays latched until the intervals fall back under the threshold, so the
configuration has to follow the node.

Every arrival, valid or not, counts toward the storm rate. A missing node
raises no storm; when it goes missing during one, the storm state is dropped
and its return reports the missing node as recovered.

`add_node` refuses a period or rate window above one hour, a rate window
under 10 ms, more than 1000
miss periods, a degrade factor above 400 (k = 100), and a zero warm-up or
streak.

## Timer

`next_tick()` is the earliest time `on_tick` has work: a node's deadline, or
the next rate bucket of a storming node so that a storm can clear while the
node is still sending. The OS layer keeps one timer armed to it.

Each alert carries the facts behind it: the last interval, the configured
period, the learned baseline, the message count for a storm, the sequence
number for sequence faults.

## Health score

0 when the node was never seen or is missing. Otherwise start at 100 and
subtract:

- jitter, after warm-up: `min(40, 100 * deviation / period)`
- 30 while degrading
- 20 while storming
- `min(20, 5 * sequence faults)`; faults are counted up to 4, and one is
  forgiven per 10 good messages

The result is clamped to 0-100.
