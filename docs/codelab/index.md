# Codelab: a deadline watchdog on QNX 8.0

This codelab builds a small watchdog on QNX 8.0, one QNX feature at a time:

1. A process that receives messages with native message passing.
2. One timer per watched node whose expiry arrives as a pulse.
3. Priorities that keep the deadline when the input is flooded.
4. A resource manager that shows the state under `/dev/isrt`.
5. Measuring the deadline, on QNX and on Linux.

Each step runs on the QNX SDP 8.0 QEMU image. The steps are written as the
engine is built.
