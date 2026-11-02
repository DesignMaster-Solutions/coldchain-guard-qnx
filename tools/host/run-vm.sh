#!/usr/bin/env bash
# Boot the image made by build-vm.sh with QEMU on the host (TCG, no network), console to a log, and
# wait for the boot-time test line. Exits 0 only if the guest printed "coldchain: core tests rc=0".
#   tools/host/run-vm.sh <vm dir> [timeout seconds]
# Mirrors the x86_64 branch of mkqnximage's qemu/runimage, minus the bridge network.
set -euo pipefail
VM=${1:?vm dir built by build-vm.sh}; T=${2:-300}
cd "$VM"
LOG=output/qemu.out
rm -f "$LOG"
qemu-system-x86_64 -smp 2 -cpu max -m 1G -no-reboot -display none -monitor none \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 -kernel output/ifs.bin -serial file:"$LOG" \
  -object rng-random,filename=/dev/urandom,id=rng0 -device virtio-rng-pci,rng=rng0 </dev/null &
pid=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT
for ((i = 0; i < T; i++)); do
  grep -q "coldchain: core tests" "$LOG" 2>/dev/null && break
  kill -0 "$pid" 2>/dev/null || break
  sleep 1
done
sleep 2 # let the last lines reach the log
tr -d '\r' < "$LOG" | grep -v '^\s*$' | tail -40
grep -q "coldchain: core tests rc=0" "$LOG"
