# Changelog

## Unreleased

### Added

- `isrt replay`: runs an event stream through the detector with timer ticks
  where a real timer would fire; `Detector::next_tick()` for the OS timer.
- Simulator scenarios (normal, offline, degrading, storm, duplicate, reboot,
  replay of an old boot) and `./dev scenario <name>`; CI runs each through the
  engine and checks the alerts.
- Detector library: missing, degrading, storm, duplicate, reordered and
  rebooted alerts with the facts behind each (a boot counter that only rises and a
  sequence that never wraps within a boot, one alert per run of
  duplicates or stale messages), `next_tick()` for the OS timer, a learned per-node baseline and a
  0-100 health score; fixed-size state, time passed in by the caller.
- Storm rate counters saturate instead of wrapping.
- Project skeleton: CMake engine with version and platform, a test harness
  without third-party dependencies, `./dev`, simulator payload signing,
  documentation site, CI.
