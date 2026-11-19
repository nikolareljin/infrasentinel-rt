# Changelog

## Unreleased

### Added

- Detector library: missing, degrading, storm, duplicate, reordered and
  rebooted alerts with the facts behind each (sequence numbers compared as
  serial numbers, a new boot confirmed by a second message), a learned per-node baseline and a
  0-100 health score; fixed-size state, time passed in by the caller.
- Project skeleton: CMake engine with version and platform, a test harness
  without third-party dependencies, `./dev`, simulator payload signing,
  documentation site, CI.
