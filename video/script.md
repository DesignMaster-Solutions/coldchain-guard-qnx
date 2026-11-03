# Cold-Chain Guard demo: shooting and render script

## Promise and shape

For QNX Global Hackathon judges: show the project functioning on the device it was built for, QNX 8.0.0 x86_64 in QEMU, including a local staff acknowledgement, and end with the audit and inspector report from that same boot verified on the host.

Shape: real terminal footage with English narration, no face, music or invented product UI. The console recording is the actual serial console of the boot, typed by `video/live-session.py`. It is paced to the narration (idle gaps capped, the boot sped up), so no timing can be read from it. Host commands execute during rendering.

## Evidence and starting state

- Target: the image built by `tools/host/build-vm.sh`, booted with `-snapshot` and the same QEMU flags as `tools/host/run-vm.sh`. Sensor input is synthetic.
- The seven boot markers (`core tests`, `m2`, `m3`, `ack`, `m4`, `forecast`, `m5 serial`, each `coldchain: <name> rc=0`) must appear in the recording before it is used.
- No measured timing, latency, throughput or comparison is shown or spoken. M3 evidence is pass/fail against application limits, never a QNX benchmark.
- Restart resets process clock and alarm state; an unknown gap is not uninterrupted coverage. Volatile demo storage does not prove power-loss persistence. No physical sensor or notification delivery is demonstrated. An acknowledgement records a name and suppresses escalation; it does not notify anyone, clear the fault or authenticate the name, and recovery clears it. Report times are process-relative, not calendar dates.

## Beat sheet

The narration is verbatim in `video/demo.json`.

| Beat | On screen | Footage range |
|---|---|---|
| 1. QNX 8.0 boots in QEMU and checks itself | Real boot; PASS lines and `coldchain: ... rc=0` markers scroll by | start to `Startup complete` |
| 2. The fridge is a file | `uname -a`, `fridged` with a synthetic excursion, two reads: level rises | to the first `ack` write |
| 3. Acknowledge the alarm | `ack who=Clinic.Team_1` prints `ack rc=0`; read shows `ack=1` at the same level; the duplicate prints `dup rc=1` | to the first limit write |
| 4. Limits change atomically, or not at all | Valid write clears the alarm; invalid write returns `rc=1`; limits unchanged | to the first `slay fridged` |
| 5. Advice comes early | Warming fixture: `alarm=0 trend=to_high eta_s=...`, later the real alarm | to the second `slay fridged` |
| 6. Kill the daemon | `pidin` at priority `20f`, `slay -s KILL`, SLM message, new pid, device read | to the end of the session |
| 7. The portable core, on the host | `make -B test`, selected literal lines | none, command output |
| 8. The audit from that boot | `fgctl-extract.py` on the console recording: serial, audit, replay, integrity limit | none, command output |
| 9. The morning report, from this boot | `fgctl report` on the boot's M7 export, tags stripped: chain OK, acknowledged by, unknown interval, not recorded | none, command output |
| 10. Reproduce it | Links, next steps, prototype-only notice | none |

## Record and render

From the repository root, with `VM_DIR` holding an image built by `tools/host/build-vm.sh`:

```sh
asciinema rec --headless --overwrite -f asciicast-v2 --window-size 150x29 \
  -c "python3 video/live-session.py $VM_DIR" build/live.cast
agg --font-family Menlo --font-size 20 --theme asciinema --idle-time-limit 3 --fps-cap 15 --last-frame-duration 1 \
  build/live.cast build/live.gif
ffmpeg -y -i build/live.gif -vf "fps=15,format=yuv420p" -c:v libx264 -crf 18 -tune stillimage build/live.mp4
# footage range of each beat; copy into clip_from/clip_to of video/demo.json
python3 video/cast-marks.py build/live.cast 'Startup complete' "printf 'ack" "echo 'low=2000" 'slay fridged' 'slay fridged' 'live-session: end'
# the console text the host verifies, without the carriage returns the recording adds
python3 -c "import json;L=open('build/live.cast').readlines();open('build/console.log','w',encoding='latin-1').write(''.join(e[2] for e in map(json.loads,L[1:]) if e[1]=='o').replace(chr(13),''))"
rm -f build/ref.bin build/audit.bin; make build/replay_target build/fgctl && build/replay_target tests/m5-scenario.csv build/ref.bin
# the boot's own M7 acknowledgement audit, for the report scene (each must exit 0)
sed -n 's/^M7-/FGCTL-/p' build/console.log > build/m7-serial.out
python3 tools/host/fgctl-extract.py build/m7-serial.out build/m7-audit.bin build/fgctl
build/fgctl report build/m7-audit.bin > build/m7-report.html
# published evidence: the same console up to the last prompt (README verifies it without a license)
python3 -c "t=open('build/console.log',encoding='latin-1').read();open('docs/evidence/qnx-boot-console.log','w',encoding='latin-1').write(t[:t.index(chr(10)+'live-session: end')].rstrip(' ')+chr(10))"
python3 <video engine>/demo_slides.py video/demo.json --work build/render
ffmpeg -y -i build/demo.mp4 -c:v copy -af 'highpass=f=70,afftdn=nf=-30,equalizer=f=3000:t=q:w=1:g=1,loudnorm=I=-14:TP=-1.5:LRA=11' \
  -c:a aac -b:a 192k -ar 48000 -movflags +faststart build/demo-final.mp4
```

The MP4, images, audio, casts and logs stay outside git (`build/`). The engine does not enforce `mono_cmd` return codes, so run each command separately with exit-code checking; `mono_grep` selects literal lines and must not turn a failure into a claimed pass.

## Review gates

- Every marker above is `rc=0` in the recording; no `FAILED`.
- Fresh command output matches the rendered lines.
- Inspect a contact sheet of the final video: full text, honest host/target labels, nothing cut, the session's own end message outside the frame.
- `ffprobe`: H.264 video, AAC audio, duration strictly below 180 seconds.
- Narration re-read without visuals: each claim identifies what ran, where, and its limit. No clinical benefit, sensor validation, notification delivery or QNX performance claim.

## Publication metadata

Title: Cold-Chain Guard for QNX 8.0: live console, crash recovery and a verified audit

Description: A simulated fridge guard running on QNX 8.0.0 x86_64 in QEMU: real boot and serial console, `/dev/fridge0` resource manager, SLM crash restart, advisory forecast, a local staff acknowledgement that records a name and stops escalation, and the audit from the same boot verified on the host and rendered as an HTML inspector report. The console footage is real and paced to the narration. Prototype only, not a medical device; the acknowledgement notifies no one, and physical sensing, power-loss validation and external notifications remain future work. No QNX performance results are published. Source: https://github.com/DesignMaster-Solutions/coldchain-guard-qnx Companion tutorial: https://github.com/DesignMaster-Solutions/qnx8-resmgr-codelab
