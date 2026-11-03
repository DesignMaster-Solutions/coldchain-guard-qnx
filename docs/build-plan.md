# Build plan

Deadline: December 15, 2026, 2:00 pm ET. Every milestone ends with a commit and a dated line in the log at the end of this file.

| # | Milestone | Done when |
|---|---|---|
| M0 | Host and target — done 2026-11-02 | QNX SDP 8.0 installed on a Linux x86_64 host (Docker on macOS) with the non-commercial license; QEMU x86_64 image built with `mkqnximage`; the core test binary built with `qcc` runs and passes on the target; supervisor for M4 chosen (SLM) |
| M1 | Portable core — done 2026-11-02 (host and QNX 8.0 target) | Alarm core in C (excursion state machine, escalation ladder, acknowledgement, hash-chained audit records) with host unit tests; the same files compile with `qcc` |
| M2 | `fridged` — done 2026-11-02 (host and QNX target) | Resource manager registers `/dev/fridge0`; `cat /dev/fridge0` prints temperature and state; writing to it changes the limits; the simulated sensor reads scenario files |
| M3 | Timing — done 2026-11-02 (host and QNX target) | Sampler runs on a timer pulse at a fixed priority; the alarm fires within its own deadline while a lower-priority CPU hog runs (this checks our deadline, it is not a benchmark of QNX) |
| M4 | Resilience — done 2026-11-02 (host and QNX target) | After `kill -9 fridged` SLM restarts it; the audit log records the gap explicitly and the chain still verifies |
| M5 | Host tool — done 2026-11-02 (host and QNX target) | `fgctl` replays a scenario on the target and verifies the audit chain from the host |
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
- Not yet verified: physical sensor, notification delivery, watchdog, durable storage, and deployment. Resource manager verification was added in M2 below. No QNX performance measurement was made. `tools/host/` is outside M1 ownership.

## M2 portable contract and host proof

- CSV: optional `t_ms,temp_mc` header, first timestamp zero, strictly increasing unsigned milliseconds, signed 32-bit milli-Celsius or empty temperature (invalid sample); at most 4096 rows. No interpolation: hold each row until the next, and hold the final row indefinitely. Scenario time is elapsed monotonic time since daemon startup.
- A write contains all three unique keys `low`, `high`, `hyst`, in arbitrary order. Decimal overflow, unknown/duplicate/missing keys, embedded NUL and oversized commands fail atomically. Validation delegates to M1 `alarm_init`; only the three limits change, preserving alarm history and other settings. The next sample evaluates the new limits.
- Host red/green (2026-11-02): a scratch copy mutated the required-key check from `seen != 7` to `seen == 0`. The missing-key test with otherwise valid defaults failed (assertion, exit 134); original sources pass `make test` with ASan/UBSan. An initial missing-key case was invalid for another reason; added the valid-default case to isolate this check. No mutant is committed.

## M2 target integration and limits

- One dispatch thread serializes sampling and device I/O. `timer_create(CLOCK_MONOTONIC)` sends `SIGEV_PULSE` through `message_connect` to a `pulse_attach` handler; each sample calls `alarm_step`, appends every emitted event through `audit.c`, then flushes. No priority tuning or timing measurement is part of M2.
- Read snapshots are per-open and support fragmented reads and EOF. Device permissions are 0644. Writes use the core validator without resetting alarm history. Renamed the core state typedef from `alarm` to `alarm_state` to coexist with POSIX `alarm()` in `<unistd.h>`; the state machine is unchanged.
- Official API references consulted inside the installed SDP: `com.qnx.doc.neutrino.resmgr/topic/read_write_Sample_IO_READ.html`, `read_write_Sample_IO_WRITE.html`; `com.qnx.doc.neutrino.lib_ref/topic/p/pulse_attach.html`, `m/message_connect.html`, `t/timer_create.html`, `i/iofunc_mount_t.html`. No SDP content is redistributed.
- Target green: `build-vm.sh` compiles with `qcc -Vgcc_ntox86_64`, C11 and strict warnings. The boot test retains `coldchain: core tests rc=0`, then reads the device, writes new limits, checks invalid writes return `EINVAL` without changing limits, exercises bytewise reads/EOF, verifies a nonempty audit chain with excursion/recovery events and rejects corrupted records. It prints `coldchain: m2 rc=0`; `run-vm.sh` exits 0 only for both markers.
- Target red: a source copy outside the repo changed the expected initial upper limit from 8000 to 7999 in `test_fridge_target.c`. Built into `~/Backups/impressora/qnx/vm-x86_64-mutant`, it fails the initial-state assertion and prints `coldchain: m2 FAILED`; `run-vm.sh` exits 1 even though the core marker passes. Neither mutant source nor image is committed.
- Scope: the three human-scale CSVs pass host parsing; target integration uses the accelerated `tests/m2-scenario.csv`, not complete real-time runs of all three profiles. M1 simulated-time tests cover alarm escalation; target M2 checks excursion/recovery, not a deadline under load. No physical probe, notification delivery, fixed-priority guarantee, watchdog, crash recovery, fsync durability or external hash anchor has been verified. Log creation is exclusive; an existing path is refused. Hash-chain limitations from M1 still apply.

## M3 timing verification and limits

- Scheduling: the single sampler/dispatch thread uses explicit `SCHED_FIFO`, default priority 20. `-P` accepts 1–63 (an application restriction); invalid decimal input, overflow and out-of-range values fail before device/log creation. Scheduler errors are fatal. The timer pulse uses the same explicit priority. The dispatch channel uses `_NTO_CHF_FIXED_PRIORITY` to suppress receive-side priority inheritance, plus the resource-manager `_NTO_CHF_UNBLOCK` and `_NTO_CHF_DISCONNECT` flags. Device I/O remains serialized with sampling.
- `-g` (grace seconds) and `-e` (escalation-step seconds) accept 1–86400. Production defaults are unchanged: grace 900 seconds, escalation 600 seconds, period 1000 milliseconds. The three-minute grace in an M1 unit scenario is a test configuration, not the daemon default.
- Test load: `coldchain_hog` creates one explicitly scheduled FIFO worker per online CPU and pins each to that CPU using `ThreadCtl`. Workers do no I/O, sleeping or yielding. The implementation supports 1–32 CPUs and fails instead of silently omitting CPUs when the runmask cannot be applied. Readiness is sent only after every worker has set its affinity. Normal workers use priority 10, below sampler 20. The observer and hog coordinator use priority 40 so observation and cleanup can run even in the inverted-priority test. Children are launched with explicit initial FIFO priority 40 via `posix_spawn`; `fridged` applies sampler priority 20 before sensor/log initialization. The load is a test tool, not a production daemon.
- Application acceptance contract: `tests/m3-scenario.csv` crosses the upper limit at configured offset C = 100 ms. The test passes period P = 100 ms, grace G = 1 s and escalation step E = 1 s to `fridged`. A declared test allowance M = 2000 ms covers startup, audit flush and observer polling. From a `CLOCK_MONOTONIC` origin taken before spawning the daemon, the local alarm must be observed by C + G + 2P + M (one sampling period to observe the crossing, another to process grace expiry). The next ladder step must be observed by that bound + E + P + M. These are configured acceptance bounds, not measured platform results.
- The observer polls complete audit records, verifies their hash chain, and requires the temperature alarm plus local ladder level 1, then escalation level 2, before their respective bounds. Observations use the observer's monotonic clock; late catch-up cannot pass. It does not read the device during load, avoiding a blocking request or priority donation. It checks both child processes remain alive, bounds hog readiness, and signals both children before bounded reaping on pass or failure (cleanup failure also fails the test). M2's daemon is stopped and reaped before M3 starts.
- Official API topics consulted in the installed SDP: `com.qnx.doc.neutrino.lib_ref/topic/p/pthread_setschedparam.html`, `s/sigevent.html`, `c/channelcreate.html`, `d/dispatch_create_channel.html`, `t/threadctl.html`, `p/posix_spawn.html`. No SDP documentation, headers or binaries are redistributed.
- Host red/green (2026-11-02): a copy outside the repository replaced rejection below the minimum numeric option with clamping. The new invalid-priority assertion failed (`make` exit 2). Original sources pass `make -B test` with ASan/UBSan. Tests cover accepted priority endpoints, invalid zero/high/signed/whitespace/trailing/overflow inputs, unchanged output on failure and accelerated-setting bounds.
- Target green (2026-11-02): `qcc -Vgcc_ntox86_64`, C11, strict warnings; QNX 8.0.0 x86_64 in QEMU with two CPUs. Boot prints `coldchain: core tests rc=0`, `coldchain: m2 rc=0` and `coldchain: m3 rc=0`; `run-vm.sh` requires all three and exits 0.
- Target red (2026-11-02): an external source copy changes only the test hog priority from 10 to 30, above sampler 20. The separate image prints `coldchain: m3 FAILED`; `run-vm.sh` exits 1 while core/M2 still pass. Green image: `~/Backups/impressora/qnx/vm-x86_64-m3`; red image: `~/Backups/impressora/qnx/vm-x86_64-m3-mutant`. Commands, return codes and pass/fail evidence stay outside the repository in `~/Backups/impressora/qnx/m3-proof/result.txt`. Neither mutant is committed.
- Limits: this is pass/fail acceptance against our configuration, not a benchmark or performance evaluation of QNX. No measured latency, jitter, histogram or platform comparison is published. It establishes neither worst-case execution time nor a universal guarantee under arbitrary I/O, higher-priority work, memory pressure, client traffic or hardware. Physical sensor, notification delivery, supervision, crash recovery and durable storage remain unverified. Audit limitations from M1/M2 still apply; an audit ladder event is not proof that a contact received a notification.

## Predictive pre-alarm (portable core, Innovation and AI criterion)

- `src/core/forecast.c` keeps the newest 16 valid readings (at most one per configurable spacing, so 60 s spacing gives a 15-minute window), fits an ordinary least-squares line anchored at the newest reading and extrapolates it to the configured limits. It returns UNKNOWN (fewer than 6 readings), STEADY (no limit within the horizon), TO_HIGH or TO_LOW with the slope in milli-Celsius per minute and the time until the fitted line meets the limit, or OUTSIDE (the alarm path owns it). Strictly increasing timestamps, caller-owned storage, no allocation, I/O or libm; a sensor gap or limit change resets the window so a trend never spans a gap.
- It is advice for staff ("warming, about 13 minutes to the upper limit"), never an alarm: it does not raise, delay or clear anything in `alarm.c`, which stays the single source of alarm events (advisory audit hints are separate).
- Host proof (2026-11-02): `make test` runs `tests/test_forecast.c` under ASan/UBSan: argument contract, spacing and time-order rules, exact ETA on rising and falling ramps, alternating noise stays STEADY, OUTSIDE, ring window forgets the older trend, and a compressor-failure warming curve (Newton warming toward a 25 C room, two-hour time constant, one-minute steps) gets the pre-alarm 15 readings (minutes) before the first reading above 8 C, with a promised ETA never longer than the time actually left. Red: a copy outside the repository that anchors the fitted value at the mean time instead of the newest reading fails `test_linear_rise` (exit 134). `qcc -Vgcc_ntox86_64` compiles the module and its test with `-Wall -Wextra -Werror`.
- Daemon integration (2026-11-02): every valid sample feeds the caller-owned forecast; invalid samples and accepted limit writes reset it, and evaluation always uses current limits. Portable formatting exposes all five categories, slope and ETA seconds rounded up; `eta_s=-1` explicitly means no ETA outside TO_HIGH/TO_LOW. ETA is relative to the newest kept reading, not a continuously updated countdown; OUTSIDE likewise describes that kept reading. Production `-w 60000` ms spacing and `-H 1800` s horizon accept strict decimal ranges 1–3600000 and 1–86400 before creating a log/device. Type 11 (`AUDIT_FORECAST`) retains the 216-byte format and emits only on category changes involving TO_HIGH/TO_LOW, including exits and direction changes; no repeated per-sample hints, but no fixed cooldown for oscillating categories. Detail includes trend, slope and ETA; limit-write hints are synchronized before write acknowledgement. Alarm logic is unchanged and remains authoritative.
- Integration verification: host ASan/UBSan tests cover every formatted category, unavailable ETA, rounding, small buffers, option ranges and all category-transition pairs. QNX boot adds a fifth gate after M4: a synthetic 500 ms step ramp, daemon sampling 100 ms, forecast spacing 500 ms, horizon 30 s and grace 1 s. A 30-second observation bound requires positive device ETA with no active alarm before the first temperature audit event, a verified chain and exactly one TO_HIGH hint followed by one OUTSIDE hint across many samples. A limit write must immediately clear the forecast without clearing the active alarm. A subsequent missing-sensor interval and resumed valid sample must both read UNKNOWN. The reset stage has a 12-second bound; an outer 45-second guard covers device I/O. These are pass/fail bounds against our fixture, not measured QNX timing.
- Boot repeatability: the runner uses QEMU snapshot mode with temporary storage inside the VM directory, preserving the freshly built base disk across runs. The fixture assumes new logs on every boot; a previously booted base image must be rebuilt first.
- Integration red/green: an external host copy changes the TO_HIGH label to TO_LOW and fails the new format assertion (`make` exit 2). A separate target image changes only the production hint condition to emit on every TO_HIGH sample; the observer rejects the duplicate and boot prints `coldchain: forecast FAILED`, runner exit 1. Original host tests and all five QNX gates pass. Commands, return codes and console excerpts are outside git in `~/Backups/impressora/qnx/forecast-proof/result.txt`; images only in `vm-x86_64-fc` and `vm-x86_64-fc-mutant` beside it. No mutant or SDP artifact is committed.
- Independent review (2026-11-02, one more external target mutant): evaluating the forecast against the startup default limits instead of the limits in force passed all five QNX gates, because after the 8000 to 8001 write the temperature is above both and `outside` reads the same. The forecast gate now also writes `high=4000` at the steady 5000 mc tail after the sensor gap and requires `trend=outside` with `eta_s=-1` once the window refills (the defaults would give `steady`). With the stronger gate that mutant prints `coldchain: forecast FAILED` (runner exit 1) and the unmodified image passes all five gates.
- Limits: the model is linear on purpose (explainable, deterministic), advisory only, not a clinical model and not validated with a physical sensor. It does not identify curve shape, door-versus-compressor cause or product safety. No benchmark, measured latency or QNX performance evaluation is published.

## M4 resilience verification and limits

- Storage adapter: `src/fridged/storage.c` keeps POSIX I/O outside the pure C11 core. Creation uses exclusive create; existing regular files are opened without following symlinks. A nonblocking whole-file advisory write lock is held until close/process exit. All complete records are verified from genesis using the same incremental sequence/link/hash check as M1, before modifying any byte. Corruption, lock contention or I/O failure refuses startup; complete corrupt records are never repaired or skipped.
- Partial final record policy: after verifying every complete record, discard only the incomplete suffix (1–215 bytes). Append exactly one `AUDIT_GAP` per successful existing-file resume, including an existing empty file. Detail contains `interval=unknown`, `valid=0`, `discarded_bytes`, `clock=process-relative` and `state=reset`. The gap has timestamp/temperature zero as placeholders, not observations. A clean restart is also a gap because coverage cannot be inferred from the file alone. No missing sample, duration or temperature is reconstructed.
- Sample evidence: record type 10 (`AUDIT_SAMPLE`) uses the unchanged 216-byte M1 format. Every sampling cycle appends its alarm events followed by a sample with explicit validity; invalid samples serialize zero temperature. Every cycle and the restart gap use `fflush` plus `fsync`, with failure fatal. Sequence/hash continue across restarts; monotonic timestamps, scenario position, alarm state, acknowledgement and runtime limit changes reset with each process. The gap makes that session boundary explicit. This does not preserve an ongoing escalation deadline across a crash.
- Supervision: project-authored `config/slm-fridged.xml` selects `repair=restart` and a one-second initial back-off. SLM increases the wait on unsuccessful attempts and resets it after successful startup. The dedicated instance uses `/dev/slm-coldchain`, explicit recovery cap `2/min`, and pathname readiness `/dev/fridge0`. It starts after M3; system startup remains the standard mkqnximage shell flow. `build-vm.sh` includes SLM/slmctl from the installed SDK and our configuration through local snippets; SDK files exist only in the external image. No SDK source, binaries or documentation are committed.
- Acceptance configuration: accelerated period 100 ms, grace and escalation one second; initial readiness and restart each have a declared 20-second allowance, stop has five seconds, and a 60-second alarm bounds the whole observer including device/control I/O. These are our pass/fail bounds, not measured QNX results. The observer requires at least two samples before killing the process with `SIGKILL`, identifies the server using `ConnectServerInfo` on the opened device, then requires a different PID answering a real device read plus at least two post-gap samples. It stops the component through SLM, waits for process/path removal, verifies the complete stable file including exact record alignment, preserves the pre-kill prefix byte-for-byte, and requires exactly one unknown gap between the two sample groups. M3's bounded audit snapshot capacity accommodates the added sample records.
- Official installed reference topics consulted: `com.qnx.doc.neutrino.utilities/topic/s/slm.html` (repair/back-off, recovery cap, normal versus abnormal termination), `s/slmctl.html` (control interface), `com.qnx.doc.neutrino.lib_ref/topic/c/connectserverinfo.html`. Only topic names are cited; our configuration is independently authored.
- Host green: `make -B test` with ASan/UBSan verifies new/empty/existing logs, preserved prefix, sample validity, gap placement, competing-process lock refusal, all 215 partial-tail sizes, and corrupt-prefix refusal without altering even the incomplete suffix. Host red: an external copy ignores a failed complete-record verification; the refusal assertion fails (`make` exit 2). An earlier baseline with exclusive-create-only behavior also fails the new resume assertion. No mutation is committed.
- Independent review (2026-11-02, three more external mutants of `storage.c`): dropping the advisory lock fails the competing-writer assertion (`make` exit 2). A gap that claims `valid=1`, or one that carries a nonzero temperature, passed the original host suite; only the QNX observer checked those fields. `test_storage.c` now checks the whole gap contract (type, zero temperature, `interval=unknown`, `valid=0`, `state=reset`) at all three gap sites, and both mutants fail (`make` exit 2) while the unmodified build passes.
- Target green: `build-vm.sh` plus `run-vm.sh` on QNX 8.0.0 x86_64 QEMU prints all four markers (`core tests`, `m2`, `m3`, `m4`, each `rc=0`) and runner exit 0. Target red: a separate source/image copy changes only the SLM repair action to `none`; M0–M3 pass, M4 prints `coldchain: m4 FAILED`, runner exit 1. Images are restricted to `~/Backups/impressora/qnx/vm-x86_64-m4` and `vm-x86_64-m4-mutant`. Commands, return codes and console evidence are in `~/Backups/impressora/qnx/m4-proof/result.txt`, outside git.
- Limits: one injected process death, not a power-cut/storage fault campaign, repeated crash storm, stalled-process watchdog or supervisor self-recovery test. The boot log lives in volatile `/tmp`; no persistent media, directory-entry durability or storage-controller guarantees are established. Locking is advisory and assumes cooperating writers and a stable private pathname. Truncation and gap append are not a transaction: a second crash during recovery can lose the discarded-byte disclosure or leave another partial gap; the next successful resume still declares an unknown interval. Hashes detect corruption, not malicious rewrite/tail removal without an external trusted anchor. Physical sensing, notifications and medical suitability remain unverified. No measured restart time, latency, jitter or QNX performance comparison is published.

## M5 host tool

- `build/fgctl verify <audit.bin>` is portable C11, using `audit_verify_file` from
  the existing core (which shares `audit_verify_next` with `audit_verify`), never
  an independent verifier. It snapshots input to a private temporary file before
  verification and summary, so the summary describes the bytes verified. RAM is
  bounded; temporary disk space is proportional to the input. Verify a stopped
  writer/export: snapshotting a live file is not an atomic filesystem snapshot.
- Exit 0 means the complete chain verifies, including an empty file (explicitly
  zero records, no observations, no first/last time). Exit 1 means broken sequence,
  link, hash or partial tail, with the first zero-based bad index. Any size not
  divisible by 216 is corruption, including a 1–215-byte file. Suffix length is
  reported, input is never changed. This preserves M4 policy: only storage resume
  can discard an incomplete suffix after validating the prefix, disclosing it in
  a gap; verify cannot silently claim that partial input is intact. Exit 2 means
  usage or file/snapshot I/O failure.
- Summary counts all 11 types individually plus unknown types, prints first/last
  timestamps in record order, escaped gap details, and excursion starts/ends.
  Gaps terminate open episodes with unknown end and increment the session; EOF
  reports an unclosed episode. Process-relative clocks are not comparable across
  gaps; zero in a gap is a placeholder. This verifies chain integrity, not the
  semantic truth of a measurement or medical safety.
- `fgctl check-scenario <csv>` uses the same `scenario_load_ex` implementation as
  the existing `scenario_load` wrapper. Capacity remains 4096 rows and all previous
  CSV rules remain; added diagnostics identify the one-based line and reason.
  Returns 0 valid, 1 invalid, 2 usage/file error. No duplicate CSV parser.
- `tools/host/fgctl-replay.sh <csv> <new-vm-dir> [timeout-seconds]` checks the CSV,
  copies it into a new external VM directory, builds with the existing SDK volume,
  boots and verifies the serial transfer. Existing VM directories are refused;
  `build-vm.sh` also refuses an existing image output. Direct builds default to
  `tests/m5-scenario.csv`. Snapshot-mode QEMU preserves the new base disk.
- Target replay after forecast uses production alarm, forecast, portable and
  storage modules. It steps simulated CSV time once per row, using default alarm
  and forecast settings; each sample is synchronized. It stops at the last row.
  This is a deterministic target replay, not the live daemon timer path and not
  a thermal/deadline test. It never sleeps for scenario time or extrapolates an
  unobserved hold after EOF. Sparse scenarios only evaluate deadlines at rows.
- Serial protocol v1: one BEGIN, hex DATA lines (up to 216 bytes each), one END
  declaring total bytes and SHA-256 of the original file. The host extractor
  rejects malformed/duplicate/incomplete framing, unexpected lines inside the
  frame, empty exports and length/hash mismatches, then invokes the C verifier.
  SHA-256 here checks transport, not authorship; an attacker replacing data and
  checksum still needs a trusted external final anchor to be detected.
- Host verification: `make -B test` with ASan/UBSan passes core, fridge, storage,
  forecast and new fgctl tests. Fixtures are generated by the core, not committed
  binaries. New tests cover every type count, gap detail, excursion bounds,
  middle/last hash corruption, middle deletion, all 215 partial tails with input
  preservation, wrong size, empty/missing files, CSV validity/line/capacity and
  CLI exit codes. Serial tests exercise replay → export → extractor → actual
  `fgctl` process and reject changed bytes, missing lines/end, duplicates and noise.
- Host red: one production mutation in an external copy counts `AUDIT_GAP` as
  `AUDIT_SAMPLE`; the new summary assertion fails (test process exit 134).
- Target green: QNX 8.0.0 x86_64 QEMU passes the unchanged core, M2, M3, M4 and
  forecast gates, followed by `coldchain: m5 serial rc=0` and host checksum/chain
  verification (`coldchain: m5 host rc=0`, runner exit 0). The default fixture
  produces 21 records, including 12 samples and two advisory transitions.
- Target red: a separate source/image changes one exported byte after hashing
  the original data. The original host runner rejects the SHA-256 mismatch,
  prints `coldchain: m5 host rc=1` and exits 1, while all five earlier gates and
  the target export completion marker pass. No mutant is committed.
- Independent review: (1) host: a summary that never counts `ALARM_RECOVERY`
  passed both suites, because `recovery=1;` also matches inside
  `sensor recovery=1;`. Both tests now assert the whole counts line; the mutant
  fails (`test_fgctl` exit 134, `test_serial.py` exit 1). (2) Serial: a frame that
  lost its last record but re-declares length and SHA-256 is self-consistent and
  its chain prefix verifies, so the extractor and `fgctl verify` both accept it.
  A target replay that drops its final CSV row exports exactly that. The runner
  now builds the same replay for the host (`-ffp-contract=off`, so the forecast
  rounds like the x86_64 target), runs it on the CSV in the VM directory and
  requires the received audit to be byte-identical, reporting the first differing
  record index. The host test covers the truncated frame (accepted without the
  reference, rejected at record index 20 with it), and an extractor that ignores
  the reference fails it. On QNX, the unmodified image passes the new runner
  (`replay OK: target audit is byte-identical to the host replay (21 records)`,
  exit 0). A separate image with the one-row replay mutation passes all six gates
  of the previous runner (exit 0, 18 of 21 records, excursion end not observed)
  and fails the new one (`replay MISMATCH: ... at record index=18 (target 18
  records, host 21)`, exit 1). No mutant is committed.
- Evidence: commands, return codes and excerpts in
  an external proof file (not in this repo). Images only in
  external VM directories. No SDP files,
  images or opaque binary fixtures enter git. No measured QNX performance,
  latency, benchmark or comparison is published. Physical sensing, notifications,
  durable-media crash guarantees and M6 submission/video remain outside M5.

## M7 staff acknowledgement and inspector report

- Device contract: `ack who=<name>` accepts 1–31 ASCII `[A-Za-z0-9._-]`
  characters and an optional single final newline. The portable parser preserves
  its output on invalid input; the limit parser is unchanged. The serialized
  QNX write handler calls `alarm_ack`, appends every returned event and calls
  `fridge_log_sync` before returning success. Inactive/duplicate/invalid ack
  returns `EINVAL`. No forecast reset occurs. Read text adds `ack=0|1` immediately
  after `level`. Device ownership authorizes writes; the supplied name is not
  authenticated. Ack is local, not delivery of a notification or fault recovery.
- Report contract: `fgctl report` shares the verifier's bounded-memory, private
  snapshot path. It verifies the whole chain before emitting HTML; corruption
  or partial tails return 1 with no stdout, file/storage errors return 2.
  HTML escapes every record-derived text sink, including who and raw detail.
  The self-contained English page has viewport and print styles, no script or
  external resource, a record-count/chain banner and the literal verify limit.
  Sessions are separated by distinct UNKNOWN INTERVAL blocks. Samples are
  summarized by count, valid/unavailable and valid-reading min/max, never rows.
  Event rows retain record order and use process-relative h:mm:ss, rounded down,
  explicitly not calendar dates. Clock and alarm state reset across gaps.
- Excursion cards show start, end, duration, observed min/max and each local
  acknowledgement, or `not acknowledged`. Recovery duration/extrema use the
  core event (recovery reading excluded). Incomplete excursions report unknown
  end at a gap or unobserved end at EOF and unknown complete duration; their
  extrema use observed valid excursion/sample readings only. Acknowledgements
  never carry into the next session. Fixed missing-data disclosure:
  "Not recorded by this prototype: calendar date and time, room temperature,
  vaccine inventory, actions taken." Fixed footer: "Simulated sensor. Prototype,
  not a medical device or a medical record." No claim of clinical compliance.
- Host red/green: the new parser test first fails against a rejecting stub
  (exit 134); implementation passes. The report test first fails without the
  report command (exit 134); excursion-card additions fail against the earlier
  report (exit 134). `make -B test` passes with ASan/UBSan (exit 0), including
  existing core acknowledgement/re-arm at 60 minutes, strict names/boundaries,
  unchanged parser output on rejection, read format, report ordering, per-session
  summaries, complete and incomplete excursions, gap resets, negative Celsius,
  escaped forged `<script>` who with a valid chain, escaped detail, tamper with
  empty stdout, all 215 partial-tail sizes, empty/missing input and all M1–M5 tests.
- External host mutants (none committed): permitting `/` in ack names fails
  `test_fridge` (exit 134); replacing the report HTML escaping sink with raw
  `fputs` fails `test_fgctl` (exit 134); skipping failed snapshot verification
  only for report fails its corruption/no-output assertion (exit 134). A further
  mutant omitting acknowledgement from excursion cards while keeping it in the
  timeline fails the card-scoped assertion (exit 134).
- Target acceptance: a new QNX 8.0.0 x86_64 image, cross-compiled with the existing
  SDP volume and strict `qcc` warnings. `test_ack_target` runs against live
  `fridged` on `m3-scenario.csv`, period 100 ms, forecast spacing 100 ms, grace
  and escalation one second. It waits for active alarm with a level below 3,
  rejects an invalid name before ack, acknowledges as `Clinic.Team_1`, reads
  `ack=1`, rejects duplicate/invalid ack and requires the same level and populated
  forecast for 2500 ms (more than two configured escalation periods). The 20-second
  readiness and 30-second observer bounds are acceptance limits, not measurements.
  The boot script stops/reaps the daemon, restarts the same audit, verifies reset
  acknowledgement and new samples, stops/reaps again, then verifies the complete
  stable audit with exactly one acknowledgement (exact name), one gap and samples
  on both sides. No still-running writer is used for the final check.
- Target green: standard `build-vm.sh` and `run-vm.sh` exit 0; markers for core,
  M2, M3, M4, forecast, ack and M5 serial all read `rc=0`. Host M5 verification
  still requires byte equality with its 21-record deterministic replay and passes.
  M7 additionally exports a separate checksummed serial frame, reuses the existing
  host extractor, verifies the chain and generates `output/m7-report.html`.
- External target mutants, separate source copies and images (none committed):
  accepting the device command without calling `alarm_ack` fails the ack gate;
  accepting a second ack without the core rejection also fails it. Each runner
  exits 1 with `coldchain: ack FAILED`, while core/M2/M3/M4/forecast and M5 serial
  remain green. The standard runner is used unchanged for both mutations.
- Sample provenance: `docs/evidence/sample-report.html` was generated with the
  final host `fgctl report` from the real green QNX boot's `output/m7-audit.bin`.
  It contains 49 records: 43 samples, two excursions, one temperature alarm, one
  escalation, one acknowledgement by `Clinic.Team_1` and one restart gap.
  SHA-256 of the checked binary export:
  `aa5b5f463a320d793299455d847df050ed103f251d16b7c481e57d2129b5cf43`.
  Reproduce with the README fresh-image build/boot commands and
  `build/fgctl report "$VM_DIR/output/m7-audit.bin" > report.html`.
  Live timestamps and sample counts can differ; M5 alone is deterministic.
  The final HTML was read and checked by text. `browse cf shot` on its local
  file URL returned HTTP 422; no login or alternate browser was used, so visual
  rendering on a phone and printed paper are not claimed as verified.
- Limits: no trusted external anchor; valid tail deletion or total rewrite is
  undetectable. Hash integrity does not validate record semantics or actor identity.
  A gap never supplies an outage duration or missing temperature. `/tmp` remains
  volatile, so synchronization is not durable-media/power-loss proof. No physical
  sensor, notification delivery, medical suitability or clinical compliance claim.
  No QNX benchmark, latency result or performance measurement is published.

## Log

- 2026-11-02: repository created with scope and plan; non-commercial SDP 8.0 license requested on myQNX.
- 2026-11-02: M0 done: SDP 8.0 (com.qnx.qnx800 8.0.0) installed headless in Docker; x86_64 QEMU image with `mkqnximage`; core tests pass on the target. Target: x86_64. The watchdog in M4 uses SLM, whose restart policy (with back-off) restarts a component that exits abnormally.
- 2026-11-02: M1 host core delivered: portable alarm and recovery data, SHA-256 chained audit with explicit restart gaps, sanitizer tests and red/green hysteresis mutation proof. QNX compile remains pending the separate SDP installation.
- 2026-11-02: M2 delivered: portable CSV/command/read protocol, QNX `/dev/fridge0` resource manager and timer pulses, host sanitizer tests and target integration. Host missing-key mutant and separate target expectation mutant both fail; unmodified build passes. M3 timing, M4 restart/durability and M5 host tool remain open.

- 2026-11-02: M3 delivered: explicit FIFO sampler/pulse priority, fixed-priority dispatch channel, validated timing options and per-CPU load; application alarm and first escalation deadline checks pass on QNX. Host option-validation mutant and separate inverted-priority target mutant fail. No QNX performance results published; M4–M6 remain open.
- 2026-11-02: predictive pre-alarm added to the portable core (least-squares trend and ETA to the limits, advisory only); host green, red mutant, qcc compile. Integration into `fridged` follows M4.

- 2026-11-02: M4 delivered: verified/locked audit resume with explicit unknown gaps and partial-tail disclosure, synchronized samples and SLM abnormal-exit restart with back-off. Host corruption-bypass and target no-restart mutants fail; original host and all four QNX boot gates pass. M5–M6 remain open; no QNX performance results published.
- 2026-11-02: M4 review: the host storage test now checks the full gap contract, which closes two surviving mutants (gap marked valid, gap with a temperature); the lock-removal mutant fails.

- 2026-11-02: predictive pre-alarm integrated into fridged: device trend/slope/ETA, validated production defaults, gap/limit resets and transition-only audit hints; host and five QNX boot gates green, separate host-format and target-per-sample-hint mutants red. M5–M6 remain open.
- 2026-11-02: pre-alarm review: the forecast boot gate now checks that the trend is judged against the limits in force after a device write, which closes a surviving startup-limits mutant.

- 2026-11-02: M5 delivered: portable fgctl audit summary/CSV diagnostics, deterministic QNX scenario replay and checksummed serial export verified on the host; five previous gates plus M5 pass, external host count and target serial-byte mutants fail. M6 remains open; no QNX performance results published.
- 2026-11-02: M5 review: whole-line count assertions close a surviving uncounted-recovery mutant; the runner now requires the QNX replay audit to be byte-identical to the host replay of the same CSV, which closes the self-consistent truncated export that the chain and checksum accept.

- 2026-11-02: M7 delivered: local device acknowledgement, mandatory QNX ack/restart gate and verified standalone inspector HTML with excursion summaries and missing-data disclosure. Host suite and original boot pass; parser/escaping/verification host mutants and no-ack/duplicate-ack target mutants fail. Sample report comes from the real QNX audit; M5 replay remains byte-identical.
