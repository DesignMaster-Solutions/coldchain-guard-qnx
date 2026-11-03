#!/usr/bin/env bash
# Host entry point: validate, build a fresh target, boot, extract and verify.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo 'usage: fgctl-replay.sh scenario.csv NEW-vm-directory [timeout-seconds]' >&2
  exit 2
fi
SCENARIO=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$1")
VM=$(python3 -c 'import os,sys; print(os.path.abspath(sys.argv[1]))' "$2")
make -C "$ROOT" build/fgctl
"$ROOT/build/fgctl" check-scenario "$SCENARIO"
if [ -e "$VM" ]; then echo "refusing existing VM directory: $VM" >&2; exit 2; fi
case "$VM/" in "$ROOT/"*) echo 'VM must be outside the repository' >&2; exit 2;; esac
mkdir -p "$VM"
cp "$SCENARIO" "$VM/replay.csv"
docker run --rm -v qnx-home:/root -v "$ROOT":/work:ro -v "$VM":/vm qnx-host bash /work/tools/host/build-vm.sh
"$ROOT/tools/host/run-vm.sh" "$VM" "${3:-300}"
