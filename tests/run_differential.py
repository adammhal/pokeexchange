#!/usr/bin/env python3
"""Runs the C++ engine and the Python reference over the same generated flows
and requires byte-identical output.

    tests/run_differential.py --replay build/pokex-replay --flows 10000
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--flows", type=int, default=10000)
    ap.add_argument("--per-flow", type=int, default=30)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--book", default="v0")
    a = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        script, cpp_out, py_out = tmp / "flows.txt", tmp / "cpp.txt", tmp / "py.txt"

        gen = subprocess.run(
            [sys.executable, str(ROOT / "tests" / "gen_flows.py"),
             "--flows", str(a.flows), "--per-flow", str(a.per_flow),
             "--seed", str(a.seed)],
            capture_output=True, text=True, check=True)
        script.write_text(gen.stdout)
        commands = sum(1 for l in gen.stdout.splitlines() if l and not l.startswith("---"))

        subprocess.run([a.replay, "--book", a.book, str(script), str(cpp_out)], check=False)
        subprocess.run([sys.executable, str(ROOT / "reference" / "engine.py"),
                        str(script), str(py_out)], check=True)

        cpp, py = cpp_out.read_text(), py_out.read_text()
        if cpp == py:
            print(f"differential OK ({a.book}): {a.flows} flows, {commands} commands, "
                  f"{len(cpp.splitlines())} events, byte-identical")
            return 0

        # Report the first divergence with context, so a failure is actionable.
        cl, pl = cpp.splitlines(), py.splitlines()
        for i, (x, y) in enumerate(zip(cl, pl)):
            if x != y:
                lo = max(0, i - 4)
                print(f"DIVERGED at output line {i + 1}", file=sys.stderr)
                for j in range(lo, i):
                    print(f"   both | {cl[j]}", file=sys.stderr)
                print(f"    cpp | {x}\n    py  | {y}", file=sys.stderr)
                return 1
        print(f"DIVERGED in length: cpp {len(cl)} lines, py {len(pl)} lines",
              file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
