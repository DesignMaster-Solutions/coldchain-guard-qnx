#!/usr/bin/env python3
"""Strict serial framing/checksum, then an optional byte comparison with the host replay.

Chain verification belongs to fgctl/core. The comparison catches what a valid chain
cannot: a self-consistent export that lost its last records."""
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


def first_difference(data, reference):
    """Zero-based record index of the first differing byte, or None if identical."""
    if data == reference:
        return None
    shorter = min(len(data), len(reference))
    byte = next((i for i in range(shorter) if data[i] != reference[i]), shorter)
    return byte // 216


def main():
    if len(sys.argv) not in (4, 5):
        print("usage: fgctl-extract.py qemu.out audit.bin fgctl [host-replay.bin]", file=sys.stderr)
        return 2
    try:
        data = extract(Path(sys.argv[1]).read_text(encoding="ascii", errors="replace"))
        Path(sys.argv[2]).write_bytes(data)
        print(f"serial OK: bytes={len(data)} SHA-256 verified", flush=True)
        rc = subprocess.run([sys.argv[3], "verify", sys.argv[2]], check=False).returncode
        if rc or len(sys.argv) == 4:
            return rc
        reference = Path(sys.argv[4]).read_bytes()
        index = first_difference(data, reference)
        if index is not None:
            print(f"replay MISMATCH: target audit differs from the host replay of the same scenario "
                  f"at record index={index} (target {len(data) // 216} records, host {len(reference) // 216})")
            return 1
        print(f"replay OK: target audit is byte-identical to the host replay ({len(data) // 216} records)")
        return 0
    except ValueError as error:
        print(f"serial BROKEN: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"serial I/O error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
