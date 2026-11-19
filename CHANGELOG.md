# Changelog

## Unreleased

### Added

- Detector library: missing, degrading, storm, duplicate, reordered and
  rebooted alerts with the facts behind each (sequence numbers compared as
  serial numbers, a boot counter that only rises, one alert per run of
  duplicates or stale messages), `next_tick()` for the OS timer, a learned per-node baseline and a
  0-100 health score; fixed-size state, time passed in by the caller.
- Project skeleton: CMake engine with version and platform, a test harness
  without third-party dependencies, `./dev`, simulator payload signing,
  documentation site, CI.
