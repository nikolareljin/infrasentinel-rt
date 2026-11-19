# InfraSentinel RT

A deterministic telemetry watchdog for QNX 8.0. Nodes publish telemetry over
MQTT; the engine detects missing, late, flooding and impersonated telemetry
within a measured deadline and stops the cell it guards.

The engine is C++17. On QNX it runs as prioritized processes that talk over
native message passing, keep one timer per node whose expiry arrives as a
pulse, and publish their state through a resource manager under `/dev/isrt`.
The same detector code builds on Linux, which gives a baseline for every
timing number.

Status: the repository is being built during the QNX Global Hackathon 2026.
The pages below fill in as each part lands.
