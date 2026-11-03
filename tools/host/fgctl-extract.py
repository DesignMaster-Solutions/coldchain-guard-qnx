#!/usr/bin/env python3
"""Strict serial framing/checksum only. Chain verification belongs to fgctl/core."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys


def extract(console):
    state = "before"
    data = bytearray()
    for line in console.splitlines():
        if line == "FGCTL-AUDIT-BEGIN v1":
            if state != "before":
                raise ValueError("duplicate or nested audit frame")
            state = "data"
        elif line.startswith("FGCTL-AUDIT-END"):
            match = re.fullmatch(r"FGCTL-AUDIT-END bytes=([0-9]+) sha256=([0-9a-f]{64})", line)
            if state != "data" or not match:
                raise ValueError("unexpected/malformed end marker")
            if len(data) != int(match[1]) or hashlib.sha256(data).hexdigest() != match[2]:
                raise ValueError("serial length/SHA-256 mismatch")
            if not data:
                raise ValueError("replay contains no audit records")
            state = "done"
        elif line.startswith("FGCTL-DATA"):
            if state != "data" or not re.fullmatch(r"FGCTL-DATA (?:[0-9a-f]{2}){1,216}", line):
                raise ValueError("unexpected/malformed data line")
            data.extend(bytes.fromhex(line[11:]))
        elif state == "data" or line.startswith("FGCTL-"):
            raise ValueError("unexpected line in audit frame")
    if state != "done":
        raise ValueError("missing complete audit frame")
    return data


def main():
    if len(sys.argv) != 4:
        print("usage: fgctl-extract.py qemu.out audit.bin fgctl", file=sys.stderr)
        return 2
    try:
        data = extract(Path(sys.argv[1]).read_text(encoding="ascii", errors="replace"))
        Path(sys.argv[2]).write_bytes(data)
        print(f"serial OK: bytes={len(data)} SHA-256 verified", flush=True)
        return subprocess.run([sys.argv[3], "verify", sys.argv[2]], check=False).returncode
    except ValueError as error:
        print(f"serial BROKEN: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"serial I/O error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
