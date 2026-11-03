#!/usr/bin/env python3
"""Exercise actual replay/export/extract/CLI and reject damaged serial frames."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("extract", "tools/host/fgctl-extract.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SerialTest(unittest.TestCase):
    def test_roundtrip_and_damage(self):
        with tempfile.TemporaryDirectory(prefix="fgctl-serial-") as tmp:
            tmp = Path(tmp)
            run = subprocess.run(["build/replay_target", "tests/m5-scenario.csv", str(tmp / "audit.bin")],
                                 capture_output=True, text=True, check=True)
            console = run.stdout
            data = module.extract("boot\n" + console + "coldchain: m5 serial rc=0\n")
            self.assertEqual(data, (tmp / "audit.bin").read_bytes())
            lines = console.splitlines()
            damaged = lines.copy()
            damaged[1] = damaged[1][:-2] + ("00" if damaged[1][-2:] != "00" else "ff")
            cases = ["\n".join(damaged), "\n".join(lines[:-2] + lines[-1:]),
                     "\n".join(lines[:-1]), console + console,
                     console.replace("FGCTL-DATA ", "FGCTL-DATA zz", 1),
                     console.replace("FGCTL-DATA ", "noise\nFGCTL-DATA ", 1), "boot only\n"]
            for broken in cases:
                with self.assertRaises(ValueError):
                    module.extract(broken)
            (tmp / "console").write_text(console)
            rc = subprocess.run(["python3", "tools/host/fgctl-extract.py", str(tmp / "console"),
                                 str(tmp / "received.bin"), str(Path("build/fgctl").resolve())],
                                capture_output=True, text=True)
            self.assertEqual(rc.returncode, 0, rc.stdout + rc.stderr)
            self.assertIn("audit OK: records=21", rc.stdout)
            self.assertIn("\nunknown=0; excursion=1; temperature alarm=1; sensor failure=1; sensor recovery=1; "
                          "escalation=2; acknowledgement=0; rearm=0; recovery=1; restart gap=0; sample=12; "
                          "forecast hint=2\n", rc.stdout)
            self.assertIn("start_ms=360000 end_ms=2040000", rc.stdout)
            self.assertEqual(subprocess.run(["build/fgctl"], capture_output=True).returncode, 2)
            # A frame that lost its last record but re-declares length and SHA-256 is
            # self-consistent and its chain prefix is valid: only the host replay sees it.
            records = [line for line in lines if line.startswith("FGCTL-DATA ")]
            kept = bytes.fromhex("".join(line[11:] for line in records[:-1]))
            short = "\n".join([lines[0]] + records[:-1] + [
                f"FGCTL-AUDIT-END bytes={len(kept)} sha256={hashlib.sha256(kept).hexdigest()}"]) + "\n"
            self.assertEqual(module.extract(short), kept)
            (tmp / "short").write_text(short)
            def cli(console_file, *reference):
                return subprocess.run(["python3", "tools/host/fgctl-extract.py", str(tmp / console_file),
                                       str(tmp / "received.bin"), str(Path("build/fgctl").resolve()),
                                       *map(str, reference)], capture_output=True, text=True)
            self.assertEqual(cli("short").returncode, 0)  # the limit, without a reference
            rc = cli("short", tmp / "audit.bin")
            self.assertEqual(rc.returncode, 1, rc.stdout + rc.stderr)
            self.assertIn("replay MISMATCH", rc.stdout)
            self.assertIn("record index=20 (target 20 records, host 21)", rc.stdout)
            rc = cli("console", tmp / "audit.bin")
            self.assertEqual(rc.returncode, 0, rc.stdout + rc.stderr)
            self.assertIn("replay OK: target audit is byte-identical to the host replay (21 records)", rc.stdout)


if __name__ == "__main__":
    unittest.main()
