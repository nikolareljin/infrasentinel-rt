# Simulator

The simulator (`simulator/isrt_sim`) produces node traffic with injected
faults. Every scenario is seeded, so it produces the same events each run
and its alerts can be checked exactly.

```bash
./dev scenario list
./dev scenario offline      # events through the engine, alerts on stdout
cd simulator && python3 -m isrt_sim events storm > storm.events
```

| Scenario | Fault | Expected alerts |
|---|---|---|
| normal | none; three nodes at 100 ms, 500 ms and 1 s with 10% jitter | none |
| offline | esp01's link down from 20 s to 40 s (no restart) | missing about 0.5 s after the last message, then recovered |
| degrading | esp02 at three times its period from 30 s | degrading |
| storm | esp01 at 2000 messages per second for 2 s | storm, then recovered (storm) |
| duplicate | esp03 sends each message twice for 5 s | duplicate |
| reboot | esp02 restarts at 20 s | rebooted |
| replay | esp01 restarts at 10 s; from 20 s to 24 s it is silent while old messages from its first boot are replayed | rebooted once; the replays are stale (reordered), missing fires during them, recovered (missing) at 24 s |

`simulator/tests/test_scenarios.py` runs each scenario through the Linux
build of the engine; CI builds the engine first and requires it.

Payload signing for the live path (HMAC-SHA256 over `dev|boot|seq|up_ms`,
16 bytes, hex) is in `isrt_sim/payload.py`.
