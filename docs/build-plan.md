# Build plan

Deadline: December 15, 2026, 2:00 pm ET. Every milestone ends with a commit and a dated line in the log at the end of this file.

| # | Milestone | Done when |
|---|---|---|
| M0 | Host and target | QNX SDP 8.0 installed on a Linux x86_64 host (Docker on macOS) with the non-commercial license; QEMU x86_64 image built with `mkqnximage`; a hello-world built with `qcc` runs on the target; confirm the High Availability Manager ships in SDP 8.0 |
| M1 | Portable core | Alarm core in C (excursion state machine, escalation ladder, acknowledgement, hash-chained audit records) with host unit tests; the same files compile with `qcc` |
| M2 | `fridged` | Resource manager registers `/dev/fridge0`; `cat /dev/fridge0` prints temperature and state; writing to it changes the limits; the simulated sensor reads scenario files |
| M3 | Timing | Sampler runs on a timer pulse at a fixed priority; the alarm fires within its own deadline while a lower-priority CPU hog runs (this checks our deadline, it is not a benchmark of QNX) |
| M4 | Resilience | After `kill -9 fridged` the watchdog restarts it; the audit log records the gap explicitly and the chain still verifies |
| M5 | Host tool | `fgctl` replays a scenario on the target and verifies the audit chain from the host |
| M6 | Delivery | Build and run instructions (SDP version, target, toolchain), codelab, demo video under 3 minutes, Devpost submission |

## Log

- 2026-11-02: repository created with scope and plan; non-commercial SDP 8.0 license requested on myQNX.
