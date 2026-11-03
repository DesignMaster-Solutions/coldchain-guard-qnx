#!/usr/bin/env python3
"""Drive a real QNX 8.0 QEMU boot over its serial console, so the demo footage is the actual terminal.

  asciinema rec --headless --window-size 150x29 -c "python3 video/live-session.py <vm dir>" live.cast

Boots with -snapshot, so the built disk stays untouched. Same image and QEMU flags as tools/host/run-vm.sh,
except that the serial port is a socket this script types into.
"""
import os
import random
import re
import select
import socket
import subprocess
import sys
import tempfile
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
# A synthetic excursion with a one-second grace and escalation, then the same daemon under a dedicated SLM instance.
SESSION = [
    ("uname -a", 2),
    ("/system/bin/fridged -s /system/etc/m3-scenario.csv -p 100 -g 1 -e 1 -l /tmp/live-audit.bin &", 1),
    ("waitfor /dev/fridge0 20", 1),
    ("cat /dev/fridge0", 3),
    ("cat /dev/fridge0", 3),
    ("echo 'low=2000 high=9500 hyst=300' > /dev/fridge0", 2),
    ("cat /dev/fridge0", 3),
    ("echo 'low=9000 high=1000 hyst=300' > /dev/fridge0; echo write rc=$?", 3),
    ("cat /dev/fridge0", 3),
    ("slay fridged", 1),
    ("/system/bin/fridged -s /system/etc/forecast-scenario.csv -p 100 -w 500 -H 30 -g 1 -l /tmp/live-forecast.bin &", 1),
    ("waitfor /dev/fridge0 20", 1),
    ("sleep 6; cat /dev/fridge0", 3),
    ("sleep 5; cat /dev/fridge0", 3),
    ("slay fridged", 1),
    ("/system/bin/slm -n /dev/slm-coldchain -R 2/min -s all -x all /system/etc/slm-fridged.xml &", 1),
    ("waitfor /dev/fridge0 20", 1),
    ("pidin -p fridged", 2),
    ("slay -s KILL fridged", 3),
    ("waitfor /dev/fridge0 20", 1),
    ("pidin -p fridged", 2),
    ("cat /dev/fridge0", 3),
]


class Console:
    def __init__(self, sock):
        self.sock, self.text = sock, ""

    def pump(self, seconds):
        """Copy console output to stdout for up to `seconds`; False on EOF."""
        ready, _, _ = select.select([self.sock], [], [], seconds)
        if not ready:
            return True
        data = self.sock.recv(4096)
        if not data:
            return False
        sys.stdout.write(data.decode("latin-1"))
        sys.stdout.flush()
        self.text += data.decode("latin-1")
        return True

    def wait(self, predicate, timeout, what):
        end = time.monotonic() + timeout
        while not predicate():
            if time.monotonic() > end or not self.pump(0.2):
                sys.exit(f"\nlive-session: gave up waiting for {what}")

    def at_prompt(self):
        return ANSI.sub("", self.text).rstrip("\r\n").endswith("# ")

    def run(self, command, hold):
        for ch in command:
            self.sock.sendall(ch.encode())
            self.pump(random.uniform(0.03, 0.08))
        self.text = ""
        self.sock.sendall(b"\r")
        self.wait(self.at_prompt, 60, f"prompt after {command!r}")
        end = time.monotonic() + hold
        while time.monotonic() < end:
            self.pump(0.2)


def main(vm):
    with tempfile.TemporaryDirectory(dir="/tmp") as tmp:
        path = f"{tmp}/serial"
        env = {**os.environ, "TMPDIR": f"{vm}/output"}
        qemu = subprocess.Popen(
            ["qemu-system-x86_64", "-snapshot", "-smp", "2", "-cpu", "max", "-m", "1G", "-no-reboot",
             "-display", "none", "-monitor", "none", "-nic", "none",
             "-drive", "file=output/disk-qemu.vmdk,if=ide,id=drv0", "-kernel", "output/ifs.bin",
             "-serial", f"unix:{path},server=on,wait=on",
             "-object", "rng-random,filename=/dev/urandom,id=rng0", "-device", "virtio-rng-pci,rng=rng0"],
            cwd=vm, env=env, stdin=subprocess.DEVNULL)
        try:
            sock = socket.socket(socket.AF_UNIX)
            for _ in range(100):
                try:
                    sock.connect(path)
                    break
                except OSError:
                    time.sleep(0.2)
            console = Console(sock)
            console.wait(lambda: "Startup complete" in console.text, 900, "boot")
            console.wait(lambda: "x86_64" in console.text.split("Startup complete")[1], 30, "banner")
            time.sleep(1)
            console.text = ""
            sock.sendall(b"\r")
            console.wait(console.at_prompt, 60, "first prompt")
            sys.stdout.write("\x1b[2J\x1b[H")  # footage only: start the live part on a clean screen
            for command, hold in SESSION:
                console.run(command, hold)
            print("\nlive-session: end", flush=True)
        finally:
            qemu.terminate()
            qemu.wait(timeout=20)


if __name__ == "__main__":
    main(os.path.abspath(sys.argv[1]))
