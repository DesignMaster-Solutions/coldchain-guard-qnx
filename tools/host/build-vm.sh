#!/usr/bin/env bash
# Cross-compile the core test binary with qcc and build a QNX 8.0 QEMU image that runs it at boot.
#   docker build -t qnx-host tools/host
#   docker run --rm -v qnx-home:/root -v "$PWD":/work -v <vm dir>:/vm qnx-host bash /work/tools/host/build-vm.sh
# The image (QNX binaries) stays in <vm dir>, outside this repo. Run it with tools/host/run-vm.sh.
set -euo pipefail
# shellcheck disable=SC1091
source "$HOME/qnx800/qnxsdp-env.sh" >/dev/null
ARCH=${ARCH:-x86_64}
# Never build over a previously generated image.
if [ -e /vm/output ]; then echo "refusing existing image output" >&2; exit 2; fi
mkdir -p /vm/bin /vm/local/snippets
if [ ! -f /vm/replay.csv ]; then cp /work/tests/m5-scenario.csv /vm/replay.csv; fi
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -I/work/src/core -I/work/src/fridged \
  /work/tools/host/replay-target.c /work/src/fridged/portable.c /work/src/fridged/storage.c /work/src/core/*.c -o /vm/bin/coldchain_replay
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -I/work/src/core \
  /work/tests/test_core.c /work/src/core/alarm.c /work/src/core/audit.c /work/src/core/sha256.c \
  -o /vm/bin/test_core
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -D_QNX_SOURCE -I/work/src/core -I/work/src/fridged \
  /work/src/fridged/main.c /work/src/fridged/storage.c /work/src/fridged/portable.c /work/src/core/*.c -o /vm/bin/fridged
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -I/work/src/core -I/work/src/fridged \
  /work/tests/test_fridge_target.c /work/src/fridged/portable.c /work/src/core/*.c -o /vm/bin/test_fridge_target
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -D_QNX_SOURCE -I/work/src/core -I/work/src/fridged \
  /work/src/hog/main.c /work/src/fridged/portable.c /work/src/core/*.c -o /vm/bin/hog
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -D_QNX_SOURCE -I/work/src/core \
  /work/tests/test_timing_target.c /work/src/core/*.c -o /vm/bin/test_timing_target
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -D_QNX_SOURCE -I/work/src/core -I/work/src/fridged \
  /work/tests/test_resilience_target.c /work/src/core/*.c -o /vm/bin/test_resilience_target
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -D_QNX_SOURCE -I/work/src/core -I/work/src/fridged \
  /work/tests/test_forecast_target.c /work/src/core/*.c -o /vm/bin/test_forecast_target
qcc -Vgcc_nto"$ARCH" -std=c11 -Wall -Wextra -Werror -O2 -I/work/src/core -I/work/src/fridged \
  /work/tests/test_ack_target.c /work/src/core/*.c -o /vm/bin/test_ack_target
file /vm/bin/test_core /vm/bin/fridged /vm/bin/test_fridge_target
echo "[perms=555] bin/coldchain_test_core=/vm/bin/test_core" > /vm/local/snippets/system_files.custom
# SDK binaries are copied only into the local target image, never into git.
echo "[perms=555] bin/slm=$QNX_TARGET/$ARCH/usr/sbin/slm" >> /vm/local/snippets/system_files.custom
echo "[perms=555] bin/slmctl=$QNX_TARGET/$ARCH/bin/slmctl" >> /vm/local/snippets/system_files.custom
cat >> /vm/local/snippets/system_files.custom <<'FILES'
[perms=555] bin/coldchain_test_ack=/vm/bin/test_ack_target
[perms=555] bin/coldchain_replay=/vm/bin/coldchain_replay
[perms=444] etc/replay.csv=/vm/replay.csv
[perms=555] bin/coldchain_test_forecast=/vm/bin/test_forecast_target
[perms=444] etc/forecast-scenario.csv=/work/tests/forecast-scenario.csv
[perms=555] bin/coldchain_test_resilience=/vm/bin/test_resilience_target
[perms=444] etc/slm-fridged.xml=/work/config/slm-fridged.xml
[perms=555] bin/coldchain_hog=/vm/bin/hog
[perms=555] bin/coldchain_test_timing=/vm/bin/test_timing_target
[perms=444] etc/m3-scenario.csv=/work/tests/m3-scenario.csv
[perms=555] bin/fridged=/vm/bin/fridged
[perms=555] bin/coldchain_test_fridge=/vm/bin/test_fridge_target
[perms=444] etc/m2-scenario.csv=/work/tests/m2-scenario.csv
[perms=444] etc/door-ajar.csv=/work/scenarios/door-ajar.csv
[perms=444] etc/power-loss.csv=/work/scenarios/power-loss.csv
[perms=444] etc/compressor-failure.csv=/work/scenarios/compressor-failure.csv
FILES
cat > /vm/local/snippets/post_start.custom <<'SH'
echo "coldchain: M0 on $(uname -a)"
if (cd /tmp && /system/bin/coldchain_test_core); then echo "coldchain: core tests rc=0"; else echo "coldchain: core tests FAILED"; fi
/system/bin/fridged -s /system/etc/m2-scenario.csv -p 100 -l /tmp/m2-audit.bin &
fridge_pid=$!
if waitfor /dev/fridge0 20 && cat /dev/fridge0 && /system/bin/coldchain_test_fridge /tmp/m2-audit.bin && cat /dev/fridge0; then
    echo "coldchain: m2 rc=0"
else
    echo "coldchain: m2 FAILED"
fi
kill "$fridge_pid"
wait "$fridge_pid" 2>/dev/null
if /system/bin/coldchain_test_timing; then
    echo "coldchain: m3 rc=0"
else
    echo "coldchain: m3 FAILED"
fi
# Dedicated SLM instance; system boot remains the standard mkqnximage shell flow.
/system/bin/slm -n /dev/slm-coldchain -R 2/min -s all -x all /system/etc/slm-fridged.xml &
slm_pid=$!
m4_rc=1
if /system/bin/coldchain_test_resilience; then m4_rc=0; fi
/system/bin/slmctl -n /dev/slm-coldchain "stop coldchain-fridged"
kill "$slm_pid"
wait "$slm_pid" 2>/dev/null
if [ "$m4_rc" = 0 ]; then
    echo "coldchain: m4 rc=0"
else
    echo "coldchain: m4 FAILED"
fi
/system/bin/fridged -s /system/etc/forecast-scenario.csv -p 100 -w 500 -H 30 -g 1 -l /tmp/forecast-audit.bin &
forecast_pid=$!
forecast_rc=1
if waitfor /dev/fridge0 20 && /system/bin/coldchain_test_forecast; then forecast_rc=0; fi
kill "$forecast_pid"
wait "$forecast_pid" 2>/dev/null
if [ "$forecast_rc" = 0 ]; then
    echo "coldchain: forecast rc=0"
else
    echo "coldchain: forecast FAILED"
fi
# M7 uses live device I/O, then a real restart of the same audit for the report.
/system/bin/fridged -s /system/etc/m3-scenario.csv -p 100 -w 100 -g 1 -e 1 -l /tmp/m7-audit.bin &
ack_pid=$!
ack_rc=1
if waitfor /dev/fridge0 20 && /system/bin/coldchain_test_ack live; then ack_rc=0; fi
kill "$ack_pid"
wait "$ack_pid" 2>/dev/null
/system/bin/fridged -s /system/etc/m3-scenario.csv -p 100 -w 100 -g 1 -e 1 -l /tmp/m7-audit.bin &
ack_pid=$!
if ! waitfor /dev/fridge0 20 || ! /system/bin/coldchain_test_ack resumed; then ack_rc=1; fi
kill "$ack_pid"
wait "$ack_pid" 2>/dev/null
if ! /system/bin/coldchain_test_ack /tmp/m7-audit.bin; then ack_rc=1; fi
if [ "$ack_rc" = 0 ]; then
    echo "coldchain: ack rc=0"
else
    echo "coldchain: ack FAILED"
fi
if /system/bin/coldchain_replay /system/etc/replay.csv /tmp/m5-audit.bin; then
    echo "coldchain: m5 serial rc=0"
else
    echo "coldchain: m5 serial FAILED"
fi
SH
cd /vm
mkqnximage --type=qemu --arch="$ARCH" --noprompt --ssh-ident=none --hostname=coldchain --build
echo "vm: fim rc=0"
