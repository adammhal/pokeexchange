#!/usr/bin/env python3
"""Runs pokex-sim twice with the same seed and requires byte-identical output.

The engine has no clock and no randomness of its own; all the randomness lives
in the simulator's single seeded stream. If this ever fails, something has
started depending on wall time, address layout, or iteration order.
"""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import tempfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sim", required=True)
    ap.add_argument("--ticks", type=int, default=8000)
    ap.add_argument("--seed", type=int, default=99)
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        digests, sizes = [], []
        for i in (1, 2):
            out = tmp / f"session{i}.json"
            subprocess.run([a.sim, "--seed", str(a.seed), "--ticks", str(a.ticks),
                            "--out", str(out)],
                           check=True, capture_output=True)
            data = out.read_bytes()
            digests.append(hashlib.sha256(data).hexdigest())
            sizes.append(len(data))

        if digests[0] != digests[1]:
            print(f"SIM NOT DETERMINISTIC\n  run 1: {digests[0]}\n  run 2: {digests[1]}",
                  file=sys.stderr)
            return 1
        print(f"sim determinism OK: {a.ticks} ticks, {sizes[0]} bytes, "
              f"sha256 {digests[0][:16]}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
