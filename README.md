# InfraSentinel RT

A deterministic telemetry watchdog for QNX 8.0. Nodes publish telemetry over
MQTT; a C++17 engine split into prioritized QNX processes (timer pulses,
native message passing, a resource manager under `/dev/isrt`) detects missing,
late, flooding and impersonated telemetry within a measured deadline and stops
the cell it guards.

Built for the QNX Global Hackathon 2026 by Nik Reljin.

- QNX: SDP 8.0 (QNX Everywhere)
- Targets: QEMU x86_64; x86_64 hardware through the generic BSP
- Toolchain: qcc (QNX SDP 8.0); g++ or clang for the Linux build

Build and run instructions follow with the first source drop.

## License

Apache-2.0, see [LICENSE](LICENSE).
