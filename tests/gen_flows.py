#!/usr/bin/env python3
"""Generates a multi-flow command script for the differential and determinism
tests. Independent flows are separated by a '---' line, so one file can carry
thousands of scenarios and both engines only need to be started once.

    tests/gen_flows.py --flows 10000 --per-flow 30 --seed 1 > flows.txt
"""
import argparse
import random


def flow(rng, start_id, n):
    lines, issued = [], []
    for _ in range(n):
        if issued and rng.random() < 0.45:
            lines.append(f"C {rng.choice(issued)}")
            continue
        if issued and rng.random() < 0.22:
            # Modifies, covering all three priority outcomes: same price
            # smaller (keeps), same price larger (loses), new price (loses).
            target = rng.choice(issued)
            lines.append(f"M {target} {rng.randint(95, 105)} {rng.randint(1, 250)}")
            continue
        oid = start_id + len(issued)
        issued.append(oid)
        side = rng.choice("BS")
        # Flags are exercised here too, or the differential would never compare
        # the IOC, FOK and post-only paths at all.
        flag = rng.choices(["", " IOC", " FOK", " PO"], weights=[70, 12, 10, 8])[0]
        if rng.random() < 0.12:
            if flag == " PO":
                flag = ""            # post-only market is a rejection, covered separately
            lines.append(f"N {oid} {side} M 0 {rng.randint(1, 200)}{flag}")
        else:
            lines.append(
                f"N {oid} {side} L {rng.randint(95, 105)} {rng.randint(1, 200)}{flag}")
    # A few deliberately invalid messages, so the reject paths are covered too.
    if rng.random() < 0.30:
        lines.append(f"N {start_id} B L 100 0")            # zero quantity
    if rng.random() < 0.30 and issued:
        lines.append(f"N {issued[0]} B L 100 5")           # duplicate id
    if rng.random() < 0.30:
        lines.append(f"C {start_id + 100000}")             # unknown order
    if rng.random() < 0.25:
        lines.append(f"N {start_id + 500} B L 0 10")       # price below domain
    if rng.random() < 0.25:
        lines.append(f"N {start_id + 501} S L 70000 10")   # price above domain
    if rng.random() < 0.15:
        lines.append(f"N {start_id + 502} B L -3 10")      # negative price
    if rng.random() < 0.20:
        lines.append(f"N {start_id + 503} B M 0 10 PO")    # post-only market
    rng.shuffle(lines) if False else None
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--flows", type=int, default=1000)
    ap.add_argument("--per-flow", type=int, default=30)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    out = []
    for i in range(a.flows):
        if i:
            out.append("---")
        out.extend(flow(rng, 1 + i * 1000, a.per_flow))
    print("\n".join(out))


if __name__ == "__main__":
    main()
