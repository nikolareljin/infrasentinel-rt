# Build and run

| | |
|---|---|
| QNX version | QNX SDP 8.0 (QNX Everywhere) |
| Targets | QEMU x86_64 (Quick Start Target Image); x86_64 hardware through the generic x86_64 BSP |
| Toolchain | `qcc` from QNX SDP 8.0 for QNX; `g++` 11+ or `clang` 14+ with CMake 3.16+ for Linux |

## Linux build

```bash
./dev build          # configure and build into build/
./dev test           # engine, simulator and script tests
ISRT_SANITIZE=1 ./dev test-engine
```

`build/engine/isrt --version` prints the version and the platform it was
built for.

## QNX build

The QNX cross-build and the QEMU run helpers come next; this page will list
the exact commands.
