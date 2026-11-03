# Cold-Chain Guard for QNX 8.0

A vaccine-fridge guard for small clinics, built to run on QNX 8.0. It watches a fridge that must stay between 2 °C and 8 °C, raises an alarm on a fixed deadline when the temperature drifts out of range, escalates until a person acknowledges it, comes back on its own if it crashes, and keeps a tamper-evident log that an inspector can verify.

**Status (November 2, 2026):** M0–M4 done. The portable C11 core and fridge protocol pass host tests with AddressSanitizer and UndefinedBehaviorSanitizer. Cross-compiled with `qcc`, the core tests and `/dev/fridge0` integration pass on a QNX 8.0.0 x86_64 target in QEMU: reads, limit changes, invalid-write rejection and audit verification. Separate host and target mutations are detected. M3 checks the application alarm and first escalation deadlines under lower-priority CPU load; an inverted-priority target mutation fails. M4 verifies SLM crash restart, a new process serving the device, and a verified audit chain with exactly one unknown gap between samples. Host corruption-bypass and target no-restart mutations fail. The predictive pre-alarm is integrated with device reads, transition-only audit hints and its own host/target red-green checks. The host tool (M5) remains pending. Built for the QNX Global Hackathon (Robotics & Medical); see [docs/build-plan.md](docs/build-plan.md) for contracts and verification limits.

## Structure

- `src/core/alarm.h/.c`: caller-owned alarm state, actions and audit events; no allocation or I/O.
- `src/core/audit.h/.c`: fixed-format hash-chained records, buffer/file verification and restart gaps.
- `src/core/sha256.h/.c`: standalone streaming SHA-256 implementation.
- `src/fridged/main.c`: QNX `resmgr`/`iofunc` adapter with a monotonic timer delivering dispatch pulses.
- `src/fridged/portable.h/.c`: CSV sensor, atomic limit-command parser and read formatting.
- `scenarios/*.csv`: door ajar, power loss and compressor failure simulations.
- `tests/test_fridge.c`: portable protocol and scenario tests.
- `tests/test_fridge_target.c`: real device I/O and audit-chain integration on QNX.
- `src/hog/main.c`, `tests/test_timing_target.c`: per-CPU load and pass/fail application deadline acceptance on QNX.
- `src/fridged/storage.h/.c`, `tests/test_storage.c`: locked POSIX audit resume, partial-tail handling and per-sample synchronization.
- `config/slm-fridged.xml`, `tests/test_resilience_target.c`: SLM restart fixture and crash/recovery acceptance.
- `tests/test_core.c`: simulated-time scenarios, FIPS vectors and tamper detection.
- `Makefile`: `make test` builds with host `cc`, C11, strict warnings and sanitizers.

## Why

A small clinic keeps its vaccines in one or two fridges. If the door is left ajar at 6 pm or the power drops overnight, the fridge drifts out of the 2–8 °C range and often nobody notices until the next morning, when the doses may have to be thrown away. A guard that never skips a reading and never sleeps on an alarm is the kind of job a real-time OS is built for.

## What it will do

| Piece | What it does | QNX feature it uses |
|---|---|---|
| `fridged` | Resource manager that exposes `/dev/fridge0`: read the temperature and the alarm state, write the limits | Resource manager framework (`resmgr`/`iofunc`), the pathname space |
| sampler | Reads the sensor on a fixed period at a fixed priority | Timers that deliver pulses, thread priorities |
| alarm ladder | Out of range → local alarm → message → second contact, until someone acknowledges | Message passing and pulses |
| watchdog | Restarts `fridged` if it dies; stall detection remains future work | System Launch and Monitor (`slm`) restart policy |
| audit log | Append-only; every record carries the hash of the previous one, so a missing or edited record shows up | POSIX file I/O |
| sensor | Simulated sensor driven by scenario files (door ajar, power cut, compressor failure) on a QEMU target; a Raspberry Pi 5 with a 1-Wire probe is optional | QEMU x86_64 target image |
| `fgctl` (host) | Replays a scenario on the target and verifies the audit chain | Host tool |

## Build and run

You need QNX SDP 8.0 (free for non-commercial use through [QNX Everywhere](https://www.qnx.com/products/everywhere/)), Docker and QEMU. The SDP is not part of this repository; it lives on a Docker volume on a Linux x86_64 host image.

```sh
make test                                   # host: core + fridge tests with sanitizers
docker build -t qnx-host tools/host         # Linux x86_64 tools image (qcc, mkqnximage run from the SDP volume)
# one-time SDP install onto the qnx-home volume: see the header of tools/host/install-sdp.sh
docker run --rm -v qnx-home:/root -v "$PWD":/work -v "$HOME/Backups/impressora/qnx/vm-x86_64-fc":/vm qnx-host bash /work/tools/host/build-vm.sh
tools/host/run-vm.sh "$HOME/Backups/impressora/qnx/vm-x86_64-fc"          # boots QNX 8.0 in QEMU; exits 0 only if the tests pass on the target
```

`build-vm.sh` cross-compiles the core tests, `fridged`, the device integration test, the CPU hog, deadline test, resilience test and predictive pre-alarm test with `qcc`, then adds them and the scenarios to a `mkqnximage` QEMU image. At boot it runs the core tests, starts the daemon with an accelerated test fixture, runs `cat /dev/fridge0`, changes limits, rejects invalid writes, reads again and checks the audit chain, including deliberate corruption. It then runs an accelerated M3 scenario under per-CPU load and checks the local alarm and first escalation against the application deadlines using the monotonic clock. Next, a dedicated SLM instance launches the M4 fixture, the test kills the serving process, requires a different process to serve the device, stops it through SLM and verifies the entire stable audit file. `run-vm.sh` exits 0 only when **all five** markers appear: `coldchain: core tests rc=0`, `coldchain: m2 rc=0`, `coldchain: m3 rc=0`, `coldchain: m4 rc=0` and `coldchain: forecast rc=0`. After M4, a separate accelerated warming fixture checks device advice before the temperature alarm, the verified transition-only audit hints, and reset on limit changes and sensor gaps. Images stay outside the repository. The runner uses a temporary disk overlay inside the VM directory, so guest writes do not contaminate later acceptance boots; start from a freshly built image.

To run a human-scale scenario on the target (after the boot test stops its daemon):

```sh
/system/bin/fridged -s /system/etc/door-ajar.csv -p 1000 -l /tmp/door-audit.bin &
cat /dev/fridge0
echo 'low=2000 high=8000 hyst=500' > /dev/fridge0
cat /dev/fridge0
```

Options: `-s` scenario file, `-p` sampling period in milliseconds (default 1000), `-l` audit path (new or verified resume). `-P` selects a fixed `SCHED_FIFO` priority (default 20, allowed 1–63) for both dispatch and timer pulses. `-g` sets grace seconds and `-e` sets escalation-step seconds (each 1–86400); defaults remain 900 and 600 respectively. All numeric options require unsigned decimal input. Startup fails if the scheduler rejects the requested priority. The daemon runs in the foreground unless backgrounded by the shell. The device is readable by everyone and writable by its owner (root in the boot demo).

Reads return `temp_mc`, `valid`, `alarm`, `level`, `low`, `high`, `hyst`, `sample_ms` and `last_valid_ms`. Times are monotonic milliseconds since this process started, not wall-clock dates or latency measurements. With `valid=0`, `temp_mc` is the last valid value and must not be treated as a current reading. Each open gets a snapshot on its first read and then EOF, including fragmented reads; reopen for a fresh snapshot. All three limit keys are required in one write; invalid input returns `EINVAL` without changing limits. Valid writes preserve alarm history, and the next sample evaluates the new limits.

CSV rows contain `t_ms,temp_mc` (optional header), start at zero and increase strictly; an empty temperature is an unavailable sensor. Values are held until the next row, with the final row held indefinitely. These are illustrative synthetic profiles, not measured vaccine-fridge data. The boot fixture is deliberately accelerated and is not a thermal model.

Audit records use the binary M1 hash-chain format. M4 adds type 10 sample records, including validity (an invalid sample carries zero, not a fabricated temperature). Events plus each sample are flushed and `fsync`ed; errors stop the daemon. Existing logs are exclusively locked and verified from genesis before modification. A corrupt complete record prevents startup. Only an incomplete final record is removed after prefix verification; its discarded byte count is recorded in one explicit unknown-interval gap before sampling resumes. The gap also declares process-relative clock and alarm-state reset; no outage duration or missed temperature is invented. `/tmp` is a volatile demonstration destination, so `fsync` here proves the synchronization path, not power-loss persistence. See the M4 verification and limits in the build plan. M3 checks only the configured application deadlines in the documented QEMU fixture, not QNX performance or a universal timing guarantee. See the M3 verification and limits in the build plan.

## Predictive pre-alarm

Device reads also contain `trend=unknown|steady|to_high|to_low|outside`, `slope_mc_per_min` and `eta_s`. ETA is seconds rounded up from the forecast at the newest kept reading, and is **-1 (not applicable)** except for `to_high`/`to_low`; zero in those categories means an immediate prediction at the core’s millisecond resolution. This is a sampled forecast, not a countdown between readings. UNKNOWN has fewer than six kept readings; OUTSIDE refers to the newest kept reading, not necessarily the current sample.

Production defaults: `-w 60000` milliseconds between kept readings (16 points, a 15-minute window) and `-H 1800` seconds of forecast horizon. Both use strict unsigned decimal validation before device/log creation; spacing accepts 1–3600000 ms and horizon 1–86400 s. Sensor loss and every accepted limit write reset the window and immediately reevaluate against current limits; rejected writes preserve it.

Audit type 11 uses the unchanged 216-byte hash-chain format. A hint records trend, slope and ETA only when the category changes and either the previous or new category is `to_high`/`to_low`, including direction changes and exits to unknown/outside. Repeated predictions never emit one hint per sample; oscillating categories can still emit one per transition, so this is not a fixed wall-clock cooldown. Limit-write hints are synchronized before acknowledging the write.

This explainable linear extrapolation is advisory only. `alarm.c` alone raises, escalates and clears alarms; the prediction cannot delay or suppress them. It is not a clinical model, has not been validated with a physical sensor and does not establish vaccine safety. Target checks are pass/fail against our synthetic configuration, not QNX benchmarks.

## Codelab

[Your first QNX 8.0 resource manager, built and booted from a Mac](https://github.com/DesignMaster-Solutions/qnx8-resmgr-codelab) is published under Apache-2.0, with green and red checks on the target.

## Notes

- This repository never contains QNX SDP files or target images.
- No benchmark of QNX itself is published here.
- This is a prototype for learning and demonstration, not a certified medical device.

## License

[Apache-2.0](LICENSE). Built by Design Master Solutions.
