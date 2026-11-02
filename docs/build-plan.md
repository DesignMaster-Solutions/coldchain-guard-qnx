# Build plan

Deadline: December 15, 2026, 2:00 pm ET. Every milestone ends with a commit and a dated line in the log at the end of this file.

| # | Milestone | Done when |
|---|---|---|
| M0 | Host and target — done 2026-11-02 | QNX SDP 8.0 installed on a Linux x86_64 host (Docker on macOS) with the non-commercial license; QEMU x86_64 image built with `mkqnximage`; the core test binary built with `qcc` runs and passes on the target; supervisor for M4 chosen (SLM) |
| M1 | Portable core — done 2026-11-02 (host and QNX 8.0 target) | Alarm core in C (excursion state machine, escalation ladder, acknowledgement, hash-chained audit records) with host unit tests; the same files compile with `qcc` |
| M2 | `fridged` | Resource manager registers `/dev/fridge0`; `cat /dev/fridge0` prints temperature and state; writing to it changes the limits; the simulated sensor reads scenario files |
| M3 | Timing | Sampler runs on a timer pulse at a fixed priority; the alarm fires within its own deadline while a lower-priority CPU hog runs (this checks our deadline, it is not a benchmark of QNX) |
| M4 | Resilience | After `kill -9 fridged` SLM restarts it; the audit log records the gap explicitly and the chain still verifies |
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
- Target proof (M0, 2026-11-02): `tools/host/build-vm.sh` compiles `tests/test_core.c` and the three core files with `qcc -Vgcc_ntox86_64` (gcc 12.2.0, `-Wall -Wextra -Werror -O2`, no sanitizers on the target) and boots a `mkqnximage` image; on QNX 8.0.0 x86_64 under QEMU all eight test groups print PASS and the boot script prints `coldchain: core tests rc=0`. Red check on the target: changing the upper recovery comparison in `alarm.c` from `<=` to `<` and building that into a separate image makes the boot script print the failed assertion (`test_power_and_recovery`) and `coldchain: core tests FAILED`, and `run-vm.sh` exits 1. No mutated code is committed.
- Not yet verified: physical sensor, notification delivery, resource manager, watchdog, durable storage, and deployment. No QNX performance measurement was made. `tools/host/` is outside M1 ownership.

## Log

- 2026-11-02: repository created with scope and plan; non-commercial SDP 8.0 license requested on myQNX.
- 2026-11-02: M0 done: SDP 8.0 (com.qnx.qnx800 8.0.0) installed headless in Docker; x86_64 QEMU image with `mkqnximage`; core tests pass on the target. Target: x86_64. The watchdog in M4 uses SLM, whose restart policy (with back-off) restarts a component that exits abnormally.
- 2026-11-02: M1 host core delivered: portable alarm and recovery data, SHA-256 chained audit with explicit restart gaps, sanitizer tests and red/green hysteresis mutation proof. QNX compile remains pending the separate SDP installation.

## M2 portable contract and host proof

- CSV: optional `t_ms,temp_mc` header, first timestamp zero, strictly increasing unsigned milliseconds, signed 32-bit milli-Celsius or empty temperature (invalid sample); at most 4096 rows. No interpolation: hold each row until the next, and hold the final row indefinitely. Scenario time is elapsed monotonic time since daemon startup.
- A write contains all three unique keys `low`, `high`, `hyst`, in arbitrary order. Decimal overflow, unknown/duplicate/missing keys, embedded NUL and oversized commands fail atomically. Validation delegates to M1 `alarm_init`; only the three limits change, preserving alarm history and other settings. The next sample evaluates the new limits.
- Host red/green (2026-11-02): a scratch copy mutated the required-key check from `seen != 7` to `seen == 0`. The missing-key test with otherwise valid defaults failed (assertion, exit 134); original sources pass `make test` with ASan/UBSan. An initial missing-key case was invalid for another reason; added the valid-default case to isolate this check. No mutant is committed.
