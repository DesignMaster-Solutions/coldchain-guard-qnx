# Build plan

Deadline: December 15, 2026, 2:00 pm ET. Every milestone ends with a commit and a dated line in the log at the end of this file.

| # | Milestone | Done when |
|---|---|---|
| M0 | Host and target | QNX SDP 8.0 installed on a Linux x86_64 host (Docker on macOS) with the non-commercial license; QEMU x86_64 image built with `mkqnximage`; a hello-world built with `qcc` runs on the target; confirm the High Availability Manager ships in SDP 8.0 |
| M1 | Portable core — host complete 2026-11-02; QNX compile pending | Alarm core in C (excursion state machine, escalation ladder, acknowledgement, hash-chained audit records) with host unit tests; the same files compile with `qcc` |
| M2 | `fridged` | Resource manager registers `/dev/fridge0`; `cat /dev/fridge0` prints temperature and state; writing to it changes the limits; the simulated sensor reads scenario files |
| M3 | Timing | Sampler runs on a timer pulse at a fixed priority; the alarm fires within its own deadline while a lower-priority CPU hog runs (this checks our deadline, it is not a benchmark of QNX) |
| M4 | Resilience | After `kill -9 fridged` the watchdog restarts it; the audit log records the gap explicitly and the chain still verifies |
| M5 | Host tool | `fgctl` replays a scenario on the target and verifies the audit chain from the host |
| M6 | Delivery | Build and run instructions (SDP version, target, toolchain), codelab, demo video under 3 minutes, Devpost submission |

## M1 contract and verification

- Pure C11, standard library only; caller-owned fixed storage. The alarm path does not allocate or perform I/O. Audit file I/O is outside the alarm path.
- Call `alarm_step` periodically with nondecreasing milliseconds, including invalid samples when the sensor is unavailable. Deadlines fire on the first call at/after expiry. Escalation starts when the local alarm is emitted; late calls catch up remaining levels. Acknowledgement shares the same clock and stops the common ladder until recovery or re-alarm.
- Valid readings within the configured limits reset the continuous-outside grace clock. Once an excursion begins, its recovery boundary is the inclusive hysteresis band. Duration includes time in the hysteresis margin and unavailable readings until confirmed recovery; extrema use valid excursion readings, excluding the recovery sample. This is observed episode duration, not a claim about unobserved temperatures or vaccine usability. Sensor timeout remains independent, and either fault keeps the alarm active.
- Audit records are 216 bytes with explicit big-endian integer encoding, zero-padded detail and SHA-256 over every field preceding the final hash. Recovery events serialize duration and extrema; acknowledgements serialize the actor. Verify the full chain before resuming and appending a gap after restart. A gap reports an unknown interval, not invented measurements.
- Verification returns the first broken zero-based record index, checks sequence and hash linkage, and rejects partial final records. A chain without a trusted external final sequence/hash cannot detect removal of its tail or a complete rewrite. Append mode, locking, durable writes and persistent clock/session handling belong to later integration milestones.
- Host proof: `make test`, C11 with `-Wall -Wextra -Werror -O2 -fsanitize=address,undefined`. Four SHA-256 FIPS vectors (including one million `a`), chunk boundaries, alarm scenarios, invalid input, timestamp boundaries, recovery serialization, a canonical record hash checked against Python hashlib, every-byte corruption, deletion, legitimate gap/resume, and buffer/file verification pass.
- Mutation proof: temporarily changed the upper recovery bound from `high_mc - hysteresis_mc` to `high_mc + hysteresis_mc`. `test_hysteresis` failed on premature recovery (make exit 2); restored the code and reran successfully. No mutated code is committed.
- Not yet verified: `qcc` compilation, QNX execution, physical sensor, notification delivery, resource manager, watchdog, durable storage, and deployment. No QNX performance measurement was made. `tools/host/` is outside M1 ownership.

## Log

- 2026-11-02: repository created with scope and plan; non-commercial SDP 8.0 license requested on myQNX.
- 2026-11-02: M1 host core delivered: portable alarm and recovery data, SHA-256 chained audit with explicit restart gaps, sanitizer tests and red/green hysteresis mutation proof. QNX compile remains pending the separate SDP installation.
