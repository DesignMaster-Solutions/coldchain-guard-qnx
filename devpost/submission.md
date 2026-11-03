# Cold-Chain Guard: Devpost form text

Prepared for Design Master Solutions, LLC. Form copy for the authorized Representative; not submitted.

Field coverage follows the public [event requirements](https://qnx.devpost.com/) and [official rules](https://qnx.devpost.com/rules), checked November 2, 2026. Exact labels, optional fields and character limits are checked in the authenticated form before submitting. The sections below supply the requested story fields and every content requirement disclosed publicly. Editorial notes and the evidence table are not form copy.

## Project name

Cold-Chain Guard for QNX 8.0

## Tagline

A simulated fridge guard on QNX: alarm escalation, crash recovery, and an audit trail that makes missing observations explicit.

## Organization

Design Master Solutions, LLC

## Category

Robotics & Medical

## Inspiration

A fridge door left ajar is an ordinary mistake. For a small clinic, finding it the next morning is too late to reconstruct what happened. We wanted to explore a guard that makes temperature excursions, alarm escalation and missing observations inspectable, instead of reducing everything to a reassuring green light.

We chose QNX because this problem calls for explicit scheduling, device interfaces and process supervision. Our submission is a working software prototype with a simulated sensor on QEMU. It is not a medical device, a validated monitoring system or evidence that any vaccine is safe to use.

## What it does

Cold-Chain Guard exposes a simulated fridge through `/dev/fridge0`. A POSIX read returns temperature, sensor validity, alarm state and advisory trend. An atomic write changes the temperature limits and hysteresis; invalid commands leave them unchanged.

The portable core tracks excursions, sensor loss, recovery and an escalation ladder. Acknowledgement exists in the core, but there is no deployed staff acknowledgement interface or external notification service. An audit event is not proof that a message reached someone.

An explainable linear forecast offers advice about movement toward a limit. Sensor gaps and accepted limit changes reset its window. It cannot delay, suppress or clear the actual alarm.

SLM restarts the daemon after an injected process death. The resumed audit preserves its verified prefix and declares an unknown interval, with process-relative clock and alarm-state reset. It does not invent missing readings or preserve an ongoing escalation deadline across a crash.

Our host tool, `fgctl`, validates scenario files and summarizes verified audit records. A deterministic replay runs the production core and storage modules on QNX, exports the audit over the serial console, then checks its checksum and chain on the host. Comparing that export byte for byte with a host replay of the same CSV also catches a self-consistent truncated replay. This comparison is specific to the deterministic fixture, not a guarantee for a live sensor log.

## How we built it

We separated a portable C11 core from the QNX adapter. `src/core/` contains the alarm state machine, SHA-256 audit chain and advisory forecast. `src/fridged/main.c` registers the resource manager with `resmgr_attach`, installs POSIX handlers through `iofunc`, and drives sampling with a `CLOCK_MONOTONIC` timer delivering `SIGEV_PULSE` events into the dispatch loop.

The daemon selects fixed `SCHED_FIFO` priority and a `_NTO_CHF_FIXED_PRIORITY` channel; timer pulses use the same explicit priority. `config/slm-fridged.xml` supplies restart policy and device-path readiness. Our boot scripts cross-compile with QNX SDP 8.0 `qcc -Vgcc_ntox86_64`, assemble a QEMU image with `mkqnximage` and run acceptance checks on QNX 8.0.0 x86_64.

On the host, Make builds with strict warnings, AddressSanitizer and UndefinedBehaviorSanitizer. On the target, checks exercise real resource-manager reads and writes, application deadline acceptance under lower-priority CPU load, crash restart, advisory reset behavior and serial export. These are pass/fail checks against our synthetic application fixtures. We publish no benchmark, measured timing, throughput or performance comparison of QNX.

## Challenges we ran into

A correct hash chain does not prove that an export is complete. We deliberately removed a valid tail and recomputed the transfer checksum; the remaining chain still verified. The replay workflow now requires equality with an independently run host replay of the same input. Live logs still need an external trusted final anchor to detect valid tail removal or full-chain replacement.

A restart also cannot tell us what happened while the process was absent. We made the gap part of the data model. Complete corrupt records prevent startup; only an incomplete suffix can be discarded after its prefix verifies, and the next gap records that discard. The demo uses volatile `/tmp` storage, so synchronization calls are exercised without claiming power-loss persistence.

Mutation checks exposed weaknesses in our tests, including an assertion that accidentally matched sensor recovery when it meant temperature recovery. We tightened the assertions rather than treating a green test suite as sufficient evidence.

## Accomplishments that we're proud of

We can follow one reproducible path from a CSV scenario to a QNX target replay and a host-verified audit. The same repository contains the device adapter, supervisor fixture, host tools and acceptance tests, with documented failure cases and limits.

Our demo is the real QNX console: the boot reports successful core, device, application-deadline, restart, forecast and serial-export gates, then a live session reads the device, changes its limits and kills the daemon under SLM. The audit exported by that same boot is then verified on the host against a fresh replay. Separate deliberate mutations are rejected, as recorded in `docs/build-plan.md`. Host checks do not substitute for QNX execution.

We also published a companion codelab that takes a Mac developer from headless SDP setup to a resource manager built and booted on QNX in QEMU.

## What we learned

Recovery must describe uncertainty, not hide it. A new process serving the device is useful evidence; it does not establish continuous observation. A checksummed export is useful evidence; it does not establish authenticity. A predicted crossing is useful advice; it does not establish clinical safety.

QNX resource managers let us present the guard through ordinary file operations, while explicit pulse priorities and supervision make the operating-system integration concrete and testable.

## What's next for Cold-Chain Guard

We want to validate a physical temperature probe on supported hardware, then test persistent media and power-loss behavior. A Raspberry Pi 5 and a 1-Wire probe are possible next steps, not completed integrations. External notifications, a staff acknowledgement interface, stalled-process detection and trusted audit anchoring also remain future work. Physical sensing and any clinical use require validation beyond this prototype.

## Built with

C11, QNX SDP 8.0, QNX OS 8.0.0, qcc, QNX resource managers, iofunc, POSIX timers, QNX pulses, SCHED_FIFO, System Launch and Monitor (SLM), mkqnximage, QEMU x86_64, Docker, Make, Bash, Python 3, SHA-256, AddressSanitizer, UndefinedBehaviorSanitizer.

## Public source code URL

https://github.com/DesignMaster-Solutions/coldchain-guard-qnx

## Project / additional link

https://github.com/DesignMaster-Solutions/coldchain-guard-qnx#build-and-run

## Build and run instructions

Target: QNX OS 8.0.0, x86_64 under QEMU. Toolchain: QNX SDP 8.0, `qcc -Vgcc_ntox86_64` (GCC 12.2), `mkqnximage`, Docker Linux x86_64 tools on macOS. Host tests require a C11 compiler, Make and Python 3; replay also requires Bash, Docker, QEMU and the user's own licensed SDP installation. No SDP files or target images are distributed.

Follow the complete setup and target-build commands in:
https://github.com/DesignMaster-Solutions/coldchain-guard-qnx#build-and-run

Run `make test` for host checks. After the documented SDP/Docker setup, run `tools/host/fgctl-replay.sh tests/m5-scenario.csv /absolute/path/to/a/new/external/vm-directory 300` to build, boot and verify the QNX replay. Use a new external directory. The timeout is a runner configuration, not a performance result. The README also documents direct device reads, limit writes and verification of an existing serial log against a host reference.

## Evidence of QNX use / Best Use of QNX-specific Technologies

We use QNX as the running target, not just a build label:

- `src/fridged/main.c`: `/dev/fridge0` via `resmgr_attach` and `iofunc`; `timer_create(CLOCK_MONOTONIC)`, `SIGEV_PULSE_INIT`, `message_connect` and `pulse_attach`; explicit `SCHED_FIFO` scheduling and `_NTO_CHF_FIXED_PRIORITY` dispatch channel.
- `config/slm-fridged.xml` and `tests/test_resilience_target.c`: SLM abnormal-exit restart, pathname readiness, a different process serving real device reads and a verified audit with an explicit unknown gap.
- `tools/host/build-vm.sh`: `qcc` cross-compilation and `mkqnximage` boot customization. `tools/host/run-vm.sh` checks the QNX console gates and host verification.
- `tests/test_fridge_target.c`, `tests/test_timing_target.c` and `tests/test_forecast_target.c`: QNX device I/O, pass/fail application deadline checks under lower-priority load and advisory integration.
- `tools/host/replay-target.c`, `fgctl-replay.sh` and `fgctl-extract.py`: deterministic QNX replay, serial transfer and host comparison.

Our demonstrated platform is QNX 8.0.0 x86_64 in QEMU with synthetic input. This is not a QNX performance evaluation or a hardware/medical validation. We are entering the Best Use of QNX-specific Technologies bonus with this implementation. If there is no separate bonus explanation field, retain this evidence in the project description.

## Best Codelab / Tutorial: designated URL field

https://github.com/DesignMaster-Solutions/qnx8-resmgr-codelab

## Best Codelab / Tutorial: description, if requested

Your first QNX 8.0 resource manager, built and booted from a Mac. Our Apache-2.0 codelab, written and tested in November 2026, teaches headless SDP installation under the reader's own license, `qcc` cross-compilation, a `/dev/hello` resource manager with read/write handlers, and `mkqnximage` plus QEMU boot verification. It includes prerequisites, complete commands, expected console output and troubleshooting. The source is supplied; QNX software is not redistributed.

## New or existing project / work during the submission period

New project created on November 2, 2026. The repository records the portable core, QNX adapter, target tests, SLM recovery, advisory forecast and host replay workflow built during the submission period. See the dated milestone log in `docs/build-plan.md` and repository history.

## Upstream project URL / upstream pull request

Not applicable. We selected Robotics & Medical, not Open-Source Contribution & Ports.

## Demo video URL

DEMO_VIDEO_URL

## Representative, eligibility and final submission (not form copy)

The authorized Representative must complete account-specific identity/team fields, confirm eligibility and accept the rules on behalf of Design Master Solutions, LLC. No personal identity or consent is supplied by this document. Check any additional fields in the authenticated form, paste the video URL and submit before December 15, 2026, 2:00 pm EST.

## Editorial evidence map (not a form field)

| Claim | Repository evidence |
|---|---|
| Device, timer pulses, scheduling | `src/fridged/main.c`; `tests/test_fridge_target.c`; build plan M2/M3 |
| Core ladder and acknowledgement | `src/core/alarm.c`; `tests/test_core.c`; build plan M1 |
| Forecast and resets | `src/core/forecast.c`; `tests/test_forecast.c`; `tests/test_forecast_target.c` |
| Verified resume, explicit gaps, SLM restart | `src/fridged/storage.c`; `config/slm-fridged.xml`; `tests/test_storage.c`; `tests/test_resilience_target.c` |
| Replay/serial integrity and tail limitation | `tools/host/replay-target.c`; `tools/host/fgctl-extract.py`; `tests/test_serial.py`; README M5 |
| Rejected mutations and QEMU acceptance limits | `docs/build-plan.md`, M1–M5 and predictive pre-alarm sections |
| Target footage used in video | `video/live-session.py`; `video/script.md` |
| Codelab scope and reproducibility | Companion repository `README.md`, `src/hello.c`, `scripts/build-image.sh`, `scripts/boot.sh` |

Judging alignment: technological implementation is evidenced by native QNX integration and target checks; design by the scenario-to-audit workflow and explicit error states; potential impact by the clinic-fridge problem with hardware validation still pending; innovation by advisory forecasting kept separate from alarms and explicit uncertainty on recovery. No trained AI model or clinical benefit is claimed. These are the four equally weighted criteria in the public rules, not predicted scores.
