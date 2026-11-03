#!/usr/bin/env python3
"""Print footage timestamps for video/demo.json from a live-session asciinema cast.

  python3 video/cast-marks.py live.cast 'uname -a' 'slm -n' ...   # first output after each marker, with gaps capped
Gaps are capped the same way as agg --idle-time-limit, so the times match the rendered clip.
"""
import json
import sys

CAP = 3.0  # keep in sync with the agg --idle-time-limit in video/script.md


def timeline(path, cap=CAP):
    clock, last, text = 0.0, 0.0, ""
    with open(path) as f:
        next(f)  # asciicast v2 header
        for line in f:
            t, kind, data = json.loads(line)
            clock += min(t - last, cap)
            last = t
            if kind == "o":
                text += data
                yield clock, text


def marks(path, wanted):
    """Time of each marker, in order; the same text may be asked for twice."""
    found, pending, pos = [], list(wanted), 0
    for clock, text in timeline(path):
        # shortcut: rescans the text per event, fine for a demo cast of a few thousand events
        while pending and pending[0] in text[pos:]:
            pos = text.index(pending[0], pos) + len(pending[0])
            found.append((pending.pop(0), clock))
    if pending:
        sys.exit(f"marker not found in order: {pending[0]!r}")
    return found


def self_check():
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".cast") as f:
        f.write('{"version":2}\n[1,"o","boot"]\n[100,"o","uname -a"]\n[101,"o","slm"]\n')
        f.flush()
        got = marks(f.name, ["uname -a", "slm"])
    assert got == [("uname -a", 4.0), ("slm", 5.0)], got  # 1 + capped 99 s gap, then 1 s


if __name__ == "__main__":
    self_check()
    for name, at in marks(sys.argv[1], sys.argv[2:]):
        print(f"{at:8.2f}  {name}")
