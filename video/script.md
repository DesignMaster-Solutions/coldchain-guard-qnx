# Cold-Chain Guard demo: shooting and render script

## Promise and shape

For QNX Global Hackathon judges, demonstrate a reproducible QNX simulated-fridge workflow with explicit recovery gaps and a host-verified target audit.

Shape: evidence-led feature walkthrough, English narration, no face, music or invented product UI. Existing target evidence makes a walkthrough appropriate; a build-along would exceed the requested runtime. The opening shows the successful device/recovery outcome before architecture. Target console excerpts are recorded evidence, not a live boot. Host commands execute during rendering.

## Evidence and starting state

- Read the repository README and `docs/build-plan.md` before recording. Target evidence is QNX 8.0.0 x86_64 in QEMU; sensor input is synthetic.
- Literal target lines: `/Users/alandebortolo/Backups/impressora/qnx/m5-proof/r306/qemu-green-e1e6a8a.out` (source revision `e1e6a8a`).
- Literal host summary: `/Users/alandebortolo/Backups/impressora/qnx/m5-proof/result.txt`. The final scene quotes only lines present in that file.
- Host-reference comparison was added in `5ff7a0f`; rendering rechecks the saved target export using the current reference implementation and the same `tests/m5-scenario.csv`.
- No new target boot is claimed. M3 evidence is pass/fail against application limits, never a QNX benchmark. No measured timing, latency, throughput or comparison is shown.
- Restart resets process clock and alarm state; an unknown gap is not uninterrupted coverage. Volatile demo storage does not prove power-loss persistence. No physical sensor or notification delivery is demonstrated.

## Beat sheet

The narration column is verbatim English. The screen contains concise reading cues and literal selected console output, not a reconstructed terminal recording. All scenes start from the JSON and the source files above; host-reference-dependent scenes additionally need the environment below.

| Beat / chapter | Visual, on-screen action and starting state | Exact narration | Budget / priority |
|---|---|---|---|
| 1. A fridge alarm must explain the gaps | Cold-Chain Guard for QNX 8.0; Simulated sensor • QNX 8.0.0 x86_64 in QEMU; Recorded target console; not a new QEMU boot; show literal saved console excerpt | A fridge alarm must explain what happened, and what it could not observe. We built Cold Chain Guard on QNX eight. These are actual saved QNX console results for the device and crash recovery checks. Our sensor is simulated. This is a software prototype, not a medical device. | 16.1 s / core |
| 2. One device, ordinary file operations | Read /dev/fridge0: temperature, validity, alarm and advice; Write all limits atomically; reject invalid input; QNX resource manager + monotonic timer pulses; show literal saved console excerpt | The resource manager exposes slash dev slash fridge zero. Reading returns a snapshot of temperature, validity, alarm state and advisory trend. Writing changes all limits atomically. Invalid input changes nothing. A monotonic timer delivers pulses to the dispatch loop, where sampling and device requests are serialized. | 15.4 s / core |
| 3. Scheduling and recovery, checked on QNX | Fixed FIFO priority for dispatch and timer pulses; SLM restarts an abnormally terminated process; Pass/fail application checks; no QNX performance results; show literal saved console excerpt | The sampler and pulses use explicit fixed priority. Our application deadline checks pass under lower priority CPU load. SLM restarts an abnormally terminated daemon, and the test reads the device from a different serving process. The audit declares an unknown gap. These are fixture acceptance checks, not QNX performance results. | 16.7 s / core |
| 4. Advice cannot silence an alarm | Explainable linear trend toward the configured limits; Sensor gaps and accepted limit writes reset the window; Alarm logic remains authoritative; show literal saved console excerpt | The forecast fits a linear trend and offers advice about approaching a limit. A sensor gap or accepted limit change resets its window. Advice cannot delay, suppress or clear the alarm. The saved QNX forecast gate passed. Physical sensing and clinical suitability have not been validated. | 15.4 s / cuttable |
| 5. Run the portable checks again | Executed on the host during this render; C11 • strict warnings • AddressSanitizer + UndefinedBehaviorSanitizer; Selected literal output; full command exit checked separately; execute `make -B test` in repository root | Now we run the portable checks on the host, using strict compiler warnings and sanitizers. These selected lines are real command output. The suite covers the state machine, device protocol, storage, forecast and host tools. Host execution supports the saved target evidence; it does not replace execution on QNX. | 16.4 s / core |
| 6. Replay the same input on the host | Fresh host reference from the target scenario; CSV timestamps are synthetic input, not measured performance; M5 replay steps rows; it is separate from the live timer path; execute `build/fgctl check-scenario tests/m5-scenario.csv && build/replay_target tests/m5-scenario.csv "$REF"` in repository root | We validate the same scenario used on the target and generate a fresh host reference. This deterministic replay steps the production modules once per CSV row. Scenario timestamps are synthetic input, not measured platform performance. The live timer and resource manager have their own target tests. | 15.4 s / core |
| 7. Verify the real QNX serial export | Saved QNX export: checksum, audit chain, host reference; The comparison also rejects a self-consistent truncated replay; execute `python3 tools/host/fgctl-extract.py "$QNX_CONSOLE" "$AUDIT" build/fgctl "$REF"` in repository root | Next we extract the real QNX serial export. The checksum verifies transfer, the audit verifier checks the chain, and the fresh host replay must match every byte. This closes the truncated replay case. A live audit still needs an external trusted anchor to detect a rewritten chain or valid tail removal. | 17.1 s / core |
| 8. Reproduce it. Then take it to hardware. | Source: github.com/DesignMaster-Solutions/coldchain-guard-qnx; Codelab: github.com/DesignMaster-Solutions/qnx8-resmgr-codelab; Next: physical probe, persistent media, external notifications; Prototype only • no vaccine-safety or clinical claim; show literal saved console excerpt | The verified target replay contains twenty one records. Our public source and companion codelab show how to build and boot with your own licensed SDP. Hardware sensing, power loss validation, external notifications and a staff acknowledgement interface remain future work. Follow the repository to reproduce the QEMU workflow. Built by Design Master Solutions, LLC. | 18.0 s / core |

## Runtime and cuts

390 narration words. At the configured `say` rate of 186 words/minute, estimated speech plus eight 0.6-second holds is 130.6 seconds. This is a planning estimate, not a measured speaking rate; final acceptance is `ffprobe` duration strictly below 180 seconds. Command execution occurs before encoding and adds no viewer waiting time. Cut the advisory beat first if runtime requires it; keep all scope limits and the verified replay path.

Chapter order: outcome → device → scheduling/recovery → advisory → host checks → reference → serial verification → reproduce/limits. Actual boundaries come from the rendered scene durations; no fabricated chapter timestamps.

## Local render brief

Only the three authored Markdown/JSON files enter git. MP4, scene images/audio, references and logs stay outside git. No site login, Devpost submission, YouTube upload or notification is part of this round. No third-party logos, personal identity, copyrighted music or secrets appear. `say` is the local voice provided by the requested demo engine; no paid generation API is used.

From the repository root, use a unique reference path per render because the replay creates its audit exclusively:

```sh
export QNX_CONSOLE=/Users/alandebortolo/Backups/impressora/qnx/m5-proof/r306/qemu-green-e1e6a8a.out
export REF="$(mktemp -d /tmp/coldchain-m6-ref.XXXXXX)/host.bin"
export AUDIT=/Users/alandebortolo/Backups/impressora/qnx/m6/received.bin
cld build --bg --target coldchain-video --label 'QNX M6 video' -- python3 /Users/alandebortolo/Dev/clauditor/skills/video-editing/scripts/demo_slides.py video/demo.json --work /Users/alandebortolo/Backups/impressora/qnx/m6/render
```

The requested engine does not enforce `mono_cmd` return codes. Independently run every command with exit-code checking and retain its full output. `mono_grep` selects literal lines; it must not turn a failure into a claimed pass. Keep commands and output inside the frame; inspect text bounds and final frames.

After rendering, the delivered copy uses local audio conditioning (video stream unchanged):

```sh
ffmpeg -y -i /Users/alandebortolo/Backups/impressora/qnx/m6/demo.mp4 -c:v copy -af 'highpass=f=70,afftdn=nf=-30,equalizer=f=3000:t=q:w=1:g=1,loudnorm=I=-14:TP=-1.5:LRA=11' -c:a aac -b:a 192k -ar 48000 -movflags +faststart /Users/alandebortolo/Backups/impressora/qnx/m6/demo-normalized.mp4
mv /Users/alandebortolo/Backups/impressora/qnx/m6/demo-normalized.mp4 /Users/alandebortolo/Backups/impressora/qnx/m6/demo.mp4
```

Run the post-processing through the same build queue after the render finishes. The extra true-peak headroom allows for AAC encoding.

## Review gates

- Compare every static console line to its source file; compare fresh command output to the rendered lines.
- Inspect every scene PNG and final frames at start, one third, two thirds and near the end. Confirm full text and honest host/target labels.
- `ffprobe`: H.264 video, AAC audio, duration below 180 seconds. Normalize/check narration loudness locally; do not upload it.
- Re-read narration without visuals: each claim must still identify what ran, where, and its limitation. On-screen cues also support muted viewing.
- Re-read submission against README/build plan; no clinical benefit, sensor validation, notification delivery or QNX performance claim.

## Publication metadata for part B (do not publish now)

Title: Cold-Chain Guard for QNX 8.0: simulated fridge, recovery gaps and verified audit

Description: We demonstrate a simulated fridge guard running on QNX 8.0.0 x86_64 in QEMU, using recorded target console evidence and fresh host verification. Prototype only, not a medical device; physical sensing and external notifications remain future work. No QNX performance results are published. Follow the source repository to reproduce the workflow: https://github.com/DesignMaster-Solutions/coldchain-guard-qnx

Companion tutorial: https://github.com/DesignMaster-Solutions/qnx8-resmgr-codelab

The authorized Representative will upload the final video publicly to YouTube and insert its URL into Devpost in part B. No publication or retention statistics exist yet.

## Verified part A delivery (November 2, 2026)

- Final local file: `/Users/alandebortolo/Backups/impressora/qnx/m6/demo.mp4`; 172.421354 seconds (2:52.42), 1920×1080, 30 fps, H.264 + AAC. SHA-256: `8d790608f6bf015de3fa2770651fc361b014f5916f1cde8ca97e0ba658bfcd64`.
- Final audio: -14.4 LUFS integrated, -1.4 dBFS true peak. Local per-scene Whisper transcription was read against all eight narration beats; technical spellings such as QEMU/daemon differ in transcription, without changing the stated limitations.
- Opened and visually inspected final extracted frames: `/tmp/coldchain-m6-start.png` at 0; `/tmp/coldchain-m6-third.png` at 57.474; `/tmp/coldchain-m6-two-thirds.png` at 114.948; `/tmp/coldchain-m6-end.png` at 172.221 seconds. All text fits. Every scene PNG was also inspected. Unsupported arrow glyphs in scene 7 were replaced with plain text and that scene re-rendered.
- Real host checks: `make -B test` executed during rendering; `make test`, scenario validation, reference replay and extractor independently returned success in verification job `47d77ec2dcd9`. Full host output: `m6/host-tests.log`; retained extractor output: `m6/extractor-proof.log` (both under the external backup directory above). Export checksum and chain passed; target audit matched the host reference byte for byte, 21 records.
- Queue jobs completed: initial render `f579c054210a`, verification `47d77ec2dcd9`, glyph correction `30842ee94ae5`, final media checks `6c751d27baa9`. Media metadata is in `m6/ffprobe.json`; audio analysis in `m6/loudness.txt`; transcripts in `m6/render/transcript0.txt` through `transcript7.txt`.
- Submission rechecked against README and the complete build plan, including the concurrent M5 evidence update in `f329218`. No source/test/tool/Makefile changes were made. Form content includes only supported claims and explicitly separates physical hardware, clinical validation and notification delivery from demonstrated behavior.
- Part B remains: public YouTube upload and video URL, authenticated form-field verification, Representative identity/eligibility/consent and final Devpost submission. No login, upload or submission occurred in part A.
