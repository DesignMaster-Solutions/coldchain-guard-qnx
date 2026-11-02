# Cold-Chain Guard for QNX 8.0

A vaccine-fridge guard for small clinics, built to run on QNX 8.0. It watches a fridge that must stay between 2 °C and 8 °C, raises an alarm on a fixed deadline when the temperature drifts out of range, escalates until a person acknowledges it, comes back on its own if it crashes, and keeps a tamper-evident log that an inspector can verify.

**Status (November 2, 2026):** M0–M2 done. The portable C11 core and fridge protocol pass host tests with AddressSanitizer and UndefinedBehaviorSanitizer. Cross-compiled with `qcc`, the core tests and `/dev/fridge0` integration pass on a QNX 8.0.0 x86_64 target in QEMU: reads, limit changes, invalid-write rejection and audit verification. Separate host and target mutations are detected. Fixed-priority deadline proof (M3), supervision (M4) and the host tool (M5) remain pending. Built for the QNX Global Hackathon (Robotics & Medical); see [docs/build-plan.md](docs/build-plan.md) for contracts and verification limits.

## Structure

- `src/core/alarm.h/.c`: caller-owned alarm state, actions and audit events; no allocation or I/O.
- `src/core/audit.h/.c`: fixed-format hash-chained records, buffer/file verification and restart gaps.
- `src/core/sha256.h/.c`: standalone streaming SHA-256 implementation.
- `src/fridged/main.c`: QNX `resmgr`/`iofunc` adapter with a monotonic timer delivering dispatch pulses.
- `src/fridged/portable.h/.c`: CSV sensor, atomic limit-command parser and read formatting.
- `scenarios/*.csv`: door ajar, power loss and compressor failure simulations.
- `tests/test_fridge.c`: portable protocol and scenario tests.
- `tests/test_fridge_target.c`: real device I/O and audit-chain integration on QNX.
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
| watchdog | Restarts `fridged` if it dies; a stalled sampler aborts the process so it is restarted too | System Launch and Monitor (`slm`) restart policy |
| audit log | Append-only; every record carries the hash of the previous one, so a missing or edited record shows up | POSIX file I/O |
| sensor | Simulated sensor driven by scenario files (door ajar, power cut, compressor failure) on a QEMU target; a Raspberry Pi 5 with a 1-Wire probe is optional | QEMU x86_64 target image |
| `fgctl` (host) | Replays a scenario on the target and verifies the audit chain | Host tool |

## Build and run

You need QNX SDP 8.0 (free for non-commercial use through [QNX Everywhere](https://www.qnx.com/products/everywhere/)), Docker and QEMU. The SDP is not part of this repository; it lives on a Docker volume on a Linux x86_64 host image.

```sh
make test                                   # host: core + fridge tests with sanitizers
docker build -t qnx-host tools/host         # Linux x86_64 tools image (qcc, mkqnximage run from the SDP volume)
# one-time SDP install onto the qnx-home volume: see the header of tools/host/install-sdp.sh
docker run --rm -v qnx-home:/root -v "$PWD":/work -v "$HOME/Backups/impressora/qnx/vm-x86_64":/vm qnx-host bash /work/tools/host/build-vm.sh
tools/host/run-vm.sh "$HOME/Backups/impressora/qnx/vm-x86_64"          # boots QNX 8.0 in QEMU; exits 0 only if the tests pass on the target
```

`build-vm.sh` cross-compiles the core tests, `fridged` and the device integration test with `qcc`, then adds them and the scenarios to a `mkqnximage` QEMU image. At boot it runs the core tests, starts the daemon with an accelerated test fixture, runs `cat /dev/fridge0`, changes limits, rejects invalid writes, reads again and checks the audit chain, including deliberate corruption. `run-vm.sh` exits 0 only when **both** `coldchain: core tests rc=0` and `coldchain: m2 rc=0` appear. Images stay outside the repository.

To run a human-scale scenario on the target (after the boot test stops its daemon):

```sh
/system/bin/fridged -s /system/etc/door-ajar.csv -p 1000 -l /tmp/door-audit.bin &
cat /dev/fridge0
echo 'low=2000 high=8000 hyst=500' > /dev/fridge0
cat /dev/fridge0
```

Options: `-s` scenario file, `-p` sampling period in milliseconds (default 1000), `-l` **new** audit path. The daemon runs in the foreground unless backgrounded by the shell. The device is readable by everyone and writable by its owner (root in the boot demo).

Reads return `temp_mc`, `valid`, `alarm`, `level`, `low`, `high`, `hyst`, `sample_ms` and `last_valid_ms`. Times are monotonic milliseconds since this process started, not wall-clock dates or latency measurements. With `valid=0`, `temp_mc` is the last valid value and must not be treated as a current reading. Each open gets a snapshot on its first read and then EOF, including fragmented reads; reopen for a fresh snapshot. All three limit keys are required in one write; invalid input returns `EINVAL` without changing limits. Valid writes preserve alarm history, and the next sample evaluates the new limits.

CSV rows contain `t_ms,temp_mc` (optional header), start at zero and increase strictly; an empty temperature is an unavailable sensor. Values are held until the next row, with the final row held indefinitely. These are illustrative synthetic profiles, not measured vaccine-fridge data. The boot fixture is deliberately accelerated and is not a thermal model.

Audit records are binary M1 hash-chain records. Events are flushed after each sample; errors stop the daemon. Existing log paths are refused rather than overwritten or silently resumed. Restart gaps, durable storage and supervision are M4 work; `/tmp` is only a demonstration destination. No fixed-priority or deadline claim is made in M2.

## Codelab

A step-by-step guide, "Your first QNX 8.0 resource manager on QEMU", will be written from this project.

## Notes

- This repository never contains QNX SDP files or target images.
- No benchmark of QNX itself is published here.
- This is a prototype for learning and demonstration, not a certified medical device.

## License

[Apache-2.0](LICENSE). Built by Design Master Solutions.
