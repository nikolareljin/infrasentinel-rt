# Replay

`isrt replay` reads an event stream on standard input, runs it through the
detector and prints every alert as one JSON object per line. Between events
it calls `on_tick` exactly when `Detector::next_tick()` says a timer would
fire, so the output matches what the live engine raises for the same
arrivals.

## Input

```text
# comment
node <name> <period_ms> [miss_periods]
msg  <t_ns> <name> <boot_hex> <seq>
end  <t_ns>
```

Times are monotonic nanoseconds and must not decrease. A malformed line stops
the run with exit code 1 and the line number on standard error.

## Output

```json
{"t":20512345678,"node":"esp01","kind":"missing","severity":"critical","interval":101234567,"expected":100000000,"baseline":100211000,"count":0,"seq":0}
{"t":40000000000,"node":"esp01","health":96}
```

`recovered` alerts add `"cleared"` (missing, degrading or storm). At `end`
the health of every node follows the alerts.
