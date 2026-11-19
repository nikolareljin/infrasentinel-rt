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
| `on_tick(now)` | a timer fired; the OS layer arms it to `deadline(node)` |

## Rules

| Alert | Severity | Rule |
|---|---|---|
| `missing` | critical | no message for `miss_periods` periods (default 5) after the last one; fires once, at the deadline |
| `recovered` | info | a missing node sends again, a degrading node has `degrade_streak` normal intervals, or a storm falls to half the limit; `cleared` says which |
| `degrading` | warning | after `warmup_samples` intervals (default 20), `degrade_streak` (default 3) intervals in a row above `mean + max(k * deviation, period / 2)`, k = 4 |
| `storm` | warning | more than `rate_max` messages in `rate_window` (default: 5 times the expected rate in 1 s, at least 5); counted in 10 buckets |
| `duplicate` | warning | the same sequence number again within one boot |
| `reordered` | warning | a lower sequence number within one boot |
| `rebooted` | info | a new boot id, confirmed by a second, later message of the same boot; the sequence restarts and the gap is not a sample |

Mean and deviation are exponentially weighted (alpha 1/16) and are updated
only by normal intervals, so a slow streak does not teach the baseline to
accept slowness. Intervals shorter than a quarter of the period are flood
traffic: they count toward the storm rate but are not samples. A node whose
real period changes keeps alerting until its configuration is updated.

A duplicate or reordered message does not move the deadline, and neither
does a single message carrying another boot id: a replayed message is not
proof that the node is alive. Sequence numbers are compared as serial
numbers, so the wrap from 4294967295 to 0 is forward. A timestamp earlier
than the last arrival is ignored. Every arrival, valid or not, counts toward
the storm rate. A node that goes missing during a storm drops the storm
state; its return reports the missing node as recovered.

`add_node` refuses a period or rate window above one hour, more than 1000
miss periods, and a zero warm-up or streak.

Each alert carries the facts behind it: the last interval, the configured
period, the learned baseline, the message count for a storm, the sequence
number for sequence faults.

## Health score

0 when the node was never seen or is missing. Otherwise start at 100 and
subtract:

- jitter, after warm-up: `min(40, 100 * deviation / period)`
- 30 while degrading
- 20 while storming
- `min(20, 5 * sequence faults)`; one fault is forgiven per 10 good messages

The result is clamped to 0-100.
