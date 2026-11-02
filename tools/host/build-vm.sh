#!/usr/bin/env bash
# Cross-compile the core test binary with qcc and build a QNX 8.0 QEMU image that runs it at boot.
#   docker build -t qnx-host tools/host
#   docker run --rm -v qnx-home:/root -v "$PWD":/work -v <vm dir>:/vm qnx-host bash /work/tools/host/build-vm.sh
# The image (QNX binaries) stays in <vm dir>, outside this repo. Run it with tools/host/run-vm.sh.
set -euo pipefail
# shellcheck disable=SC1091
source "$HOME/qnx800/qnxsdp-env.sh" >/dev/null
ARCH=${ARCH:-x86_64}
mkdir -p /vm/bin /vm/local/snippets
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -I/work/src/core \
  /work/tests/test_core.c /work/src/core/alarm.c /work/src/core/audit.c /work/src/core/sha256.c \
  -o /vm/bin/test_core
file /vm/bin/test_core
echo "[perms=555] bin/coldchain_test_core=/vm/bin/test_core" > /vm/local/snippets/system_files.custom
cat > /vm/local/snippets/post_start.custom <<'SH'
echo "coldchain: M0 on $(uname -a)"
if (cd /tmp && /system/bin/coldchain_test_core); then echo "coldchain: core tests rc=0"; else echo "coldchain: core tests FAILED"; fi
SH
cd /vm
mkqnximage --type=qemu --arch="$ARCH" --noprompt --ssh-ident=none --hostname=coldchain --build
echo "vm: fim rc=0"
