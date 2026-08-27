#!/usr/bin/env python3
"""An independent reference implementation of the PokeExchange matching engine.

The point of this file is to disagree with the C++ engine if either one is
wrong. So it is deliberately written in a different shape: plain dicts of lists
keyed by price, with the best price recomputed by min()/max() over the live keys
on every lookup, and no index from order id to location at all (cancel just
searches). Nothing here is shared with the C++ implementation except the file
formats and the rules themselves.

Two independent implementations agreeing on the same random flows is much
stronger evidence than any hand-written test suite.

    reference/engine.py [input] [output]      (defaults: stdin, stdout)
"""
import sys

BUY, SELL = "B", "S"
LIMIT, MARKET = "L", "M"


class Engine:
    def __init__(self):
        self.levels = {BUY: {}, SELL: {}}   # side -> {price: [order, ...]}
        self.seq = 0

    # ---------------------------------------------------------------- helpers
    def best(self, side):
        prices = [p for p, q in self.levels[side].items() if q]
        if not prices:
            return None
        return max(prices) if side == BUY else min(prices)

    def contains(self, oid):
        for side_levels in self.levels.values():
            for queue in side_levels.values():
                for o in queue:
                    if o["id"] == oid:
                        return True
        return False

    def remove(self, oid):
        for side_levels in self.levels.values():
            for price, queue in side_levels.items():
                for i, o in enumerate(queue):
                    if o["id"] == oid:
                        del queue[i]
                        if not queue:
                            del side_levels[price]
                        return o
        return None

    @staticmethod
    def crosses(order, best_other):
        if order["type"] == MARKET:
            return True                      # no price limit; price is never read
        if order["side"] == BUY:
            return best_other <= order["price"]
        return best_other >= order["price"]

    # ---------------------------------------------------------------- commands
    def new_order(self, oid, side, otype, price, qty, out):
        if qty == 0:
            out.append(f"R {oid} zero_quantity")
            return
        if self.contains(oid):
            out.append(f"R {oid} duplicate_order_id")
            return

        self.seq += 1
        seq = self.seq
        out.append(f"A {oid} {seq}")

        remaining = qty
        other = SELL if side == BUY else BUY
        order = {"id": oid, "side": side, "type": otype, "price": price}

        while remaining > 0:
            best = self.best(other)
            if best is None:
                break
            if not self.crosses({**order, "price": price}, best):
                break
            queue = self.levels[other][best]
            resting = queue[0]                       # oldest at the best price
            fill = min(remaining, resting["remaining"])
            out.append(f"T {resting['id']} {oid} {best} {fill} {seq}")
            remaining -= fill
            resting["remaining"] -= fill
            if resting["remaining"] == 0:
                del queue[0]
                if not queue:
                    del self.levels[other][best]

        if remaining == 0:
            return
        if otype == LIMIT:
            self.levels[side].setdefault(price, []).append(
                {"id": oid, "side": side, "type": otype, "price": price,
                 "quantity": qty, "remaining": remaining, "seq": seq})
        else:
            out.append(f"X {oid} {remaining}")

    def cancel(self, oid, out):
        removed = self.remove(oid)
        if removed is None:
            out.append(f"R {oid} unknown_order")
            return
        self.seq += 1
        out.append(f"X {oid} {removed['remaining']}")


def run(lines, write):
    engine = Engine()
    for lineno, raw in enumerate(lines, 1):
        line = raw.strip()
        if line.startswith("---"):
            engine = Engine()
            write("---")
            continue
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        out = []
        if parts[0] == "C":
            engine.cancel(int(parts[1]), out)
        elif parts[0] == "N":
            _, oid, side, otype, price, qty = parts
            engine.new_order(int(oid), side, otype, int(price), int(qty), out)
        else:
            print(f"reference: line {lineno}: unknown command {parts[0]!r}",
                  file=sys.stderr)
            continue
        for e in out:
            write(e)


def main(argv):
    src = open(argv[1]) if len(argv) > 1 else sys.stdin
    dst = open(argv[2], "w") if len(argv) > 2 else sys.stdout
    buf = []
    run(src, buf.append)
    dst.write("".join(e + "\n" for e in buf))
    dst.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
