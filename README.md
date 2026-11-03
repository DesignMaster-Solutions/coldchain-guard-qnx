# Cold-Chain Guard for QNX 8.0

A vaccine-fridge guard for small clinics, built to run on QNX 8.0. It watches a fridge that must stay between 2 °C and 8 °C, raises an alarm on a fixed deadline when the temperature drifts out of range, escalates until it is acknowledged, comes back on its own if it crashes, and keeps a hash-chained log that an inspector can verify. Edits inside the chain show up; removing its tail or rewriting all of it needs an external anchor, which is future work. The sensor is simulated: this is a software prototype, not a medical device.

**Status (November 2, 2026):** M0–M5 done. The portable C11 core and fridge protocol pass host tests with AddressSanitizer and UndefinedBehaviorSanitizer. Cross-compiled with `qcc`, the core tests and `/dev/fridge0` integration pass on a QNX 8.0.0 x86_64 target in QEMU: reads, limit changes, invalid-write rejection and audit verification. Separate host and target mutations are detected. M3 checks the application alarm and first escalation deadlines under lower-priority CPU load; an inverted-priority target mutation fails. M4 verifies SLM crash restart, a new process serving the device, and a verified audit chain with exactly one unknown gap between samples. Host corruption-bypass and target no-restart mutations fail. The predictive pre-alarm is integrated with device reads, transition-only audit hints and its own host/target red-green checks. The M5 host tool validates scenarios, replays them on QNX, checks the serial export checksum and verifies the audit chain on the host. Built for the QNX Global Hackathon (Robotics & Medical); see [docs/build-plan.md](docs/build-plan.md) for contracts and verification limits.

**Demo (under 3 minutes, real QNX console in QEMU):** https://youtu.be/zKRIykuno2c

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
- `src/fgctl/`: portable audit summary and scenario-check command.
- `tools/host/fgctl-replay.sh`, `replay-target.c`, `fgctl-extract.py`: target replay and checked serial transfer.
- `Makefile`: `make test` builds with host `cc`, C11, strict warnings and sanitizers.

## Why

A clinic that keeps vaccines in a fridge has to answer specific questions when the temperature leaves its range. [CDC guidance](https://www.cdc.gov/vaccines/hcp/storage-handling/temp-monitoring.html) says an excursion requires immediate action and asks staff to document when it happened, the minimum and maximum temperatures, how long the vaccine may have been affected and who reported it. A door left ajar at closing time still needs those answers the next morning, and [vaccine appearance is not a reliable indicator](https://www.cdc.gov/vaccines/hcp/storage-handling/cold-chain.html) of exposure.

Why QNX, as demonstrated on the target here: fixed priorities keep the sampler and alarm checks passing under lower-priority CPU load; a crashed daemon is one restartable process under SLM, not a reboot; and a resource manager lets any POSIX client read the fridge without a custom protocol.

## What it does

| Piece | What it does | QNX feature it uses |
|---|---|---|
| `fridged` | Resource manager that exposes `/dev/fridge0`: read the temperature and the alarm state, write the limits | Resource manager framework (`resmgr`/`iofunc`), the pathname space |
| sampler | Reads the sensor on a fixed period at a fixed priority | Timers that deliver pulses, thread priorities |
| alarm ladder | Out of range → local alarm → message → second contact, until acknowledged. Each step is an audit event; delivering it to a person is future work | Pulses driving the portable state machine |
| watchdog | Restarts `fridged` if it dies; stall detection remains future work | System Launch and Monitor (`slm`) restart policy |
| audit log | Append-only; every record carries the hash of the previous one, so a missing or edited record shows up | POSIX file I/O |
| sensor | Simulated sensor driven by scenario files (door ajar, power cut, compressor failure) on a QEMU target; a Raspberry Pi 5 with a 1-Wire probe is a possible next step, not done | QEMU x86_64 target image |
| `fgctl` (host) | Replays a scenario on the target and verifies the audit chain | Host tool |

## Build and run

**Target:** QNX OS 8.0.0, x86_64 under QEMU. **Toolchain:** QNX SDP 8.0, `qcc -Vgcc_ntox86_64` (GCC 12.2), `mkqnximage`.

No QNX license? You can still check the real target output: [docs/evidence/qnx-boot-console.log](docs/evidence/qnx-boot-console.log) is the literal serial console of the boot in the demo video, and the host verifies the audit it exported:

```sh
make build/fgctl build/replay_target
R=$(mktemp -d); build/replay_target tests/m5-scenario.csv "$R/host.bin"
python3 tools/host/fgctl-extract.py docs/evidence/qnx-boot-console.log "$R/audit.bin" build/fgctl "$R/host.bin"
```

Host helpers need a C11 compiler, Make, Bash and Python 3. Target replay also needs QNX SDP 8.0 (free for non-commercial use through [QNX Everywhere](https://www.qnx.com/products/everywhere/)), Docker and QEMU. The SDP is not part of this repository; it lives on a Docker volume on a Linux x86_64 host image.

```sh
make test                                   # host: core + fridge tests with sanitizers
export VM_DIR=/absolute/path/to/a/new/vm-directory   # outside this repo, new for each build
docker build -t qnx-host tools/host         # Linux x86_64 tools image (qcc, mkqnximage run from the SDP volume)
# one-time SDP install onto the qnx-home volume: see the header of tools/host/install-sdp.sh
docker run --rm -v qnx-home:/root -v "$PWD":/work:ro -v "$VM_DIR":/vm qnx-host bash /work/tools/host/build-vm.sh
tools/host/run-vm.sh "$VM_DIR"          # boots QNX 8.0 in QEMU; exits 0 only if the tests pass on the target
```

Use a new VM directory for each build; the commands above and the M5 replay example are alternative entry points, not consecutive builds into the same directory.

`build-vm.sh` cross-compiles the core tests, `fridged`, the device integration test, the CPU hog, deadline test, resilience test, predictive pre-alarm test and M5 replay tool with `qcc`, then adds them and the scenarios to a `mkqnximage` QEMU image. At boot it runs the core tests, starts the daemon with an accelerated test fixture, runs `cat /dev/fridge0`, changes limits, rejects invalid writes, reads again and checks the audit chain, including deliberate corruption. It then runs an accelerated M3 scenario under per-CPU load and checks the local alarm and first escalation against the application deadlines using the monotonic clock. Next, a dedicated SLM instance launches the M4 fixture, the test kills the serving process, requires a different process to serve the device, stops it through SLM and verifies the entire stable audit file. `run-vm.sh` exits 0 only when **all five existing** markers appear: `coldchain: core tests rc=0`, `coldchain: m2 rc=0`, `coldchain: m3 rc=0`, `coldchain: m4 rc=0` and `coldchain: forecast rc=0`, **plus** the M5 export marker `coldchain: m5 serial rc=0` and successful host verification, including byte equality with the host replay of the same CSV (`coldchain: m5 host rc=0`). After M4, a separate accelerated warming fixture checks device advice before the temperature alarm, the verified transition-only audit hints, and reset on limit changes and sensor gaps. Images stay outside the repository. The runner uses a temporary disk overlay inside the VM directory, so guest writes do not contaminate later acceptance boots; start from a freshly built image.

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

## M5 host tool

```sh
make build/fgctl
build/fgctl verify /path/to/audit.bin
build/fgctl check-scenario scenarios/door-ajar.csv
# Must be a NEW directory outside this repository; needs Docker, SDP volume and QEMU.
# Run a lengthy build/boot through your background job runner if applicable.
tools/host/fgctl-replay.sh tests/m5-scenario.csv "$VM_DIR" 300
# To recheck a collected serial log (no target needed) against the host replay of the same CSV:
make build/fgctl build/replay_target
ref=$(mktemp -d)/host-replay.bin
build/replay_target tests/m5-scenario.csv "$ref" > /dev/null
python3 tools/host/fgctl-extract.py /path/to/qemu.out /path/to/received.bin "$PWD/build/fgctl" "$ref"
```

`verify` is a portable C11 command using the core's verifier, with a private temporary
snapshot and bounded RAM. It reports counts of every record type, first/last
record timestamps, gap details and excursion boundaries. Timestamps are in record
order and reset across sessions, not wall-clock dates. A gap ends an open excursion
with an **unknown** end; EOF leaves it **not observed**. Integrity is not a medical
assessment or proof of notification delivery.

Exit codes: **0** intact chain / valid scenario; **1** corruption / invalid CSV;
**2** usage, file or temporary-storage error. A corrupt audit identifies the first
bad **zero-based** record index. An empty audit returns 0 with zero records and
no observations or timestamps. Any nonmultiple of 216, including an incomplete
first record, returns 1 and reports the suffix byte count. Verification never
modifies the input: M4 alone may discard that suffix when reopening for writing,
after verifying the complete prefix, and records the discard in a restart gap.
A corrupt complete record is never skipped. CSV diagnostics give the one-based
line and reason from the same parser/capacity (4096 rows) used by `fridged`.

Replay runs **on QNX**, after the five existing gates. It calls the production
alarm, forecast, CSV and synchronized storage modules at each CSV timestamp, once
per row, with default alarm/forecast settings. It finishes after the last row:
there are no interpolated samples or final hold interval. This deterministic
simulated-time path is separate from the live `fridged` timer/resource manager;
M2–M4 and forecast continue testing that adapter. Sparse rows can delay event
observation until the next row; this is not a sampling/deadline acceptance test.

The target exports hex between `FGCTL-AUDIT-BEGIN v1` and an end marker containing
the byte count and whole-file SHA-256. The host rejects missing, duplicate,
malformed, truncated or checksum-mismatched frames, writes
`<vm>/output/m5-audit.bin`, then runs `fgctl verify`. QEMU has no network. A checksum
protects the serial transfer, not authenticity: without an external trusted
final sequence/hash, valid tail removal or full-chain rewrite cannot be detected.

For the replay, the runner closes that tail gap: it builds the same portable replay
for the host (no FMA contraction, so the forecast rounds like the x86_64 target),
runs it on the CSV in the VM directory, and requires the audit received from QNX to
be byte-identical. An export that lost its last records, which a valid chain and a
matching checksum both accept, fails with the first differing record index. This
covers our deterministic replay, not a live device's audit, where the limit above
still applies.

Example from the QNX replay of `tests/m5-scenario.csv`, verified on the host:

```text
excursion session=0 start_ms=360000 end_ms=2040000 valid=1 level=2 duration_ms=1680000 min_mc=9000 max_mc=10000 who=
audit OK: records=21
unknown=0; excursion=1; temperature alarm=1; sensor failure=1; sensor recovery=1; escalation=2; acknowledgement=0; rearm=0; recovery=1; restart gap=0; sample=12; forecast hint=2
first_ms=0 last_ms=2040000 (record order; process-relative clocks reset at gaps)
```

## M7 staff acknowledgement and inspector report

An authorized local operator can acknowledge the active alarm through the same
device. This records a name and suppresses escalation; it does not notify anyone,
clear the underlying fault, authenticate the name, or reset the forecast window.
Device writes still require the owner's permissions (root in this demo).

```sh
# On QNX, while fridged is running and alarm=1:
printf 'ack who=Clinic.Team_1\n' > /dev/fridge0
cat /dev/fridge0
# On the host, using a stopped writer's audit or a checked export:
make build/fgctl
build/fgctl report /path/to/audit.bin > report.html
# After a fresh image build and the standard acceptance boot:
build/fgctl report "$VM_DIR/output/m7-audit.bin" > report.html
```

The exact command is `ack who=<name>`, optionally followed by one newline.
Names contain 1–31 ASCII characters from `[A-Za-z0-9._-]`. An inactive alarm,
duplicate acknowledgement or invalid name returns `EINVAL` without changing
state. The original all-three-key limit command is unchanged. Recovery clears
acknowledgement; a continuing fault re-arms after the existing 60-minute interval.
A restart resets alarm and acknowledgement state and records an unknown gap.

Reads now return `temp_mc`, `valid`, `alarm`, `level`, **`ack=0|1` immediately
after `level`**, `low`, `high`, `hyst`, `sample_ms`, `last_valid_ms`, `trend`,
`slope_mc_per_min` and `eta_s`. `ack=1` means locally acknowledged, not recovered.
All time fields remain process-relative; invalid temperatures and forecast ETA
retain the limits described above. An accepted ack is written to the audit and
synchronized before the write returns.

`report` emits one English HTML document with inline styling, no JavaScript
or external resources. Open it locally on a phone or print it. It verifies
a private snapshot of the complete chain **before emitting any HTML**: exit 0
means success, 1 means corruption (including a partial tail), and 2 means a
file/storage error. Failed verification leaves stdout empty; use `verify` for
record-index diagnostics. Samples become per-session counts and observed extrema;
events remain in record order. Each excursion includes its start, end, duration,
minimum, maximum and acknowledgement names, or `not acknowledged`. An unfinished
excursion has an unknown end at a gap or an unobserved end at EOF, with unknown
complete duration. Recovery extrema exclude the recovery reading, following the
core contract. Times are h:mm:ss since process start, rounded down to seconds,
not calendar dates. An **UNKNOWN INTERVAL** separates sessions and declares the
clock and alarm-state reset. Record text is escaped, including names and details.

The report explicitly lists what is not recorded: calendar date and time, room
temperature, vaccine inventory and actions taken. Integrity alone cannot detect
valid tail removal or a total rewrite without a trusted external anchor; it does
not establish the truth of recorded claims. This is a simulated-sensor prototype,
not a medical device or medical record.

The boot now additionally requires **`coldchain: ack rc=0`**. Its accelerated
`m3-scenario` fixture uses `-g 1 -e 1`, checks local acknowledgement, freezes the
ladder for more than two escalation periods, rejects duplicate/invalid names,
and preserves the forecast. The daemon stops, restarts the same audit, then
stops again before full verification: exactly one acknowledgement and one gap.
The checked M7 serial export becomes `output/m7-audit.bin` and
`output/m7-report.html`. Every previous gate, including M5 byte equality, remains
mandatory. See [the real QNX sample report](docs/evidence/sample-report.html) and
[the M7 verification evidence](docs/build-plan.md#m7-staff-acknowledgement-and-inspector-report).

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
