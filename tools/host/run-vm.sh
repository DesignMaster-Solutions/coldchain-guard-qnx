#!/usr/bin/env bash
# Boot the image made by build-vm.sh with QEMU on the host (TCG, no network), console to a log, and
# wait for five existing gates plus M5 serial export AND host verification.
#   tools/host/run-vm.sh <vm dir> [timeout seconds]
# Mirrors the x86_64 branch of mkqnximage's qemu/runimage, minus the bridge network.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
make -C "$ROOT" build/fgctl
VM=${1:?vm dir built by build-vm.sh}; T=${2:-300}
cd "$VM"
LOG=output/qemu.out
rm -f "$LOG"
# Boot fixtures require fresh logs. Keep all guest writes in a temporary overlay
# inside this VM directory; never modify the built disk across acceptance runs.
TMPDIR="$PWD/output" qemu-system-x86_64 -snapshot -smp 2 -cpu max -m 1G -no-reboot -display none -monitor none -nic none \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 -kernel output/ifs.bin -serial file:"$LOG" \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 </dev/null &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true' EXIT
for ((i = 0; i < T; i++)); do
  grep -Eq "coldchain: m5 serial (rc=0|FAILED)" "$LOG" 2>/dev/null && break
  kill -0 "$pid" 2>/dev/null || break
  sleep 1
done
sleep 2 # let the last lines reach the log
tr -d '\r' < "$LOG" | grep -vE '^([[:space:]]*$|FGCTL-DATA )' | tail -40
grep -q "coldchain: core tests rc=0" "$LOG"
grep -q "coldchain: m2 rc=0" "$LOG"
grep -q "coldchain: m3 rc=0" "$LOG"
grep -q "coldchain: m4 rc=0" "$LOG"
grep -q "coldchain: forecast rc=0" "$LOG"
grep -q "coldchain: m5 serial rc=0" "$LOG"
rc=0
python3 "$ROOT/tools/host/fgctl-extract.py" "$LOG" output/m5-audit.bin "$ROOT/build/fgctl" || rc=$?
echo "coldchain: m5 host rc=$rc"
[ "$rc" = 0 ] || exit 1
