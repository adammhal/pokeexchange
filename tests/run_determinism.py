#!/usr/bin/env python3
"""Replays one generated script twice through the C++ engine and requires the
two outputs to be byte-identical. This is the property that makes replay
testing, the Python differential, and the honest v0-vs-v3 benchmark possible.

    tests/run_determinism.py --replay build/pokex-replay
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--flows", type=int, default=500)
    ap.add_argument("--seed", type=int, default=17)
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        script = tmp / "flows.txt"
        gen = subprocess.run(
            [sys.executable, str(ROOT / "tests" / "gen_flows.py"),
             "--flows", str(a.flows), "--seed", str(a.seed)],
            capture_output=True, text=True, check=True)
        script.write_text(gen.stdout)

        digests = []
        for i in (1, 2):
            out = tmp / f"out{i}.txt"
            subprocess.run([a.replay, "--instruments", "3", str(script), str(out)],
                           check=False)
            digests.append(hashlib.sha256(out.read_bytes()).hexdigest())

        if digests[0] != digests[1]:
            print(f"NOT DETERMINISTIC\n  run 1: {digests[0]}\n  run 2: {digests[1]}",
                  file=sys.stderr)
            return 1
        print(f"determinism OK: {a.flows} flows, identical output, "
              f"sha256 {digests[0][:16]}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
