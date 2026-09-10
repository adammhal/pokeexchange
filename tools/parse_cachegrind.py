#!/usr/bin/env python3
"""Turns a valgrind cache summary into a comparison table.

Valgrind SIMULATES a cache rather than reading hardware counters, which is
exactly why it is usable here: it needs no PMU access, so it runs on a
virtualised CI runner, where `perf stat` generally cannot read hardware events
at all. The trade is that these are modelled figures rather than measurements.
The direction and rough magnitude are meaningful; the absolute numbers are not
a claim about any particular CPU.

    parse_cachegrind.py v0=out0.txt v1=out1.txt ...
"""
import re
import sys

# Whitespace between the label words varies between valgrind versions and
# between cachegrind and callgrind, so it is matched loosely rather than
# literally. Getting this wrong is how the first run produced a table with two
# columns of question marks.
FIELDS = [
    (r"I\s+refs", "insn"),
    (r"D\s+refs", "data refs"),
    (r"D1\s+misses", "D1 misses"),
    (r"D1\s+miss rate", "D1 miss rate"),
    (r"LLd\s+misses", "LLd misses"),
    (r"LLd\s+miss rate", "LLd miss rate"),
]

COLUMNS = [key for _, key in FIELDS]


def parse(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    found = {}
    for pattern, key in FIELDS:
        match = re.search(r"==\d+==\s+" + pattern + r":\s+([0-9,.%]+)", text)
        if match:
            found[key] = match.group(1)
    return found


def as_number(value):
    if not value:
        return None
    try:
        return float(value.replace(",", "").rstrip("%"))
    except ValueError:
        return None


def main(argv):
    runs = []
    for arg in argv[1:]:
        name, _, path = arg.partition("=")
        if not path:
            print(f"expected name=path, got {arg!r}", file=sys.stderr)
            return 2
        runs.append((name, parse(path)))

    if not runs:
        print("nothing to parse", file=sys.stderr)
        return 1

    baseline = as_number(runs[0][1].get("D1 misses"))

    print("| book | " + " | ".join(COLUMNS) + " | D1 misses vs " + runs[0][0] + " |")
    print("|---" * (len(COLUMNS) + 2) + "|")
    for name, found in runs:
        cells = [found.get(col, "?") for col in COLUMNS]
        mine = as_number(found.get("D1 misses"))
        if name == runs[0][0]:
            relative = "baseline"
        elif mine is not None and baseline:
            relative = f"{mine / baseline:.2f}x"
        else:
            relative = "?"
        print(f"| {name} | " + " | ".join(cells) + f" | {relative} |")

    missing = [name for name, found in runs if "D1 misses" not in found]
    if missing:
        print(f"\nNo cache figures parsed for: {', '.join(missing)}. "
              "The profiler may have run without --cache-sim=yes.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
