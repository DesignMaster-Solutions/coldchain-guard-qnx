# Cold-Chain Guard for QNX 8.0

A vaccine-fridge guard for small clinics, built to run on QNX 8.0. It watches a fridge that must stay between 2 °C and 8 °C, raises an alarm on a fixed deadline when the temperature drifts out of range, escalates until a person acknowledges it, comes back on its own if it crashes, and keeps a tamper-evident log that an inspector can verify.

**Status (November 2, 2026):** M0 and M1 done. The portable C11 core (alarm state machine, acknowledgement/re-alarm ladder, excursion recovery data, SHA-256 audit chain) passes `make test` on the host with AddressSanitizer and UndefinedBehaviorSanitizer, and the same test binary, cross-compiled with `qcc` (gcc 12.2, `gcc_ntox86_64`), passes on a QNX 8.0.0 x86_64 target booted in QEMU. The resource manager (M2) is next. Built for the QNX Global Hackathon (category: Robotics & Medical); see [docs/build-plan.md](docs/build-plan.md) for milestones and verification limits.

## Structure

- `src/core/alarm.h/.c`: caller-owned alarm state, actions and audit events; no allocation or I/O.
- `src/core/audit.h/.c`: fixed-format hash-chained records, buffer/file verification and restart gaps.
- `src/core/sha256.h/.c`: standalone streaming SHA-256 implementation.
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
make test                                   # host: core tests with sanitizers
docker build -t qnx-host tools/host         # Linux x86_64 tools image (qcc, mkqnximage run from the SDP volume)
# one-time SDP install onto the qnx-home volume: see the header of tools/host/install-sdp.sh
docker run --rm -v qnx-home:/root -v "$PWD":/work -v "$HOME/qnx-vm":/vm qnx-host bash /work/tools/host/build-vm.sh
tools/host/run-vm.sh "$HOME/qnx-vm"          # boots QNX 8.0 in QEMU; exits 0 only if the tests pass on the target
```

`build-vm.sh` cross-compiles `tests/test_core.c` with `qcc`, adds it to a `mkqnximage` QEMU image and runs it from the end of the boot script; `run-vm.sh` reads the serial console. The image stays outside the repository.

## Codelab

A step-by-step guide, "Your first QNX 8.0 resource manager on QEMU", will be written from this project.

## Notes

- This repository never contains QNX SDP files or target images.
- No benchmark of QNX itself is published here.
- This is a prototype for learning and demonstration, not a certified medical device.

## License

[Apache-2.0](LICENSE). Built by Design Master Solutions.
