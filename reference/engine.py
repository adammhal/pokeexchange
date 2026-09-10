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

MIN_PRICE, MAX_PRICE = 1, 65535

BUY, SELL = "B", "S"
LIMIT, MARKET = "L", "M"
GTC, IOC, FOK = "GTC", "IOC", "FOK"


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

    @staticmethod
    def blocks_self_trade(incoming, resting):
        """Zero means no self-match identifier, so prevention does not apply."""
        who = incoming.get("participant", 0)
        return who != 0 and who == resting.get("participant", 0)

    def available(self, other, order, needed):
        """How much of `order` could fill right now, capped at `needed`."""
        prices = sorted(p for p, q in self.levels[other].items() if q)
        if other == BUY:
            prices.reverse()                 # best bid is the highest
        total = 0
        for p in prices:
            if not self.crosses(order, p):
                break                        # priority order, so the rest are worse
            for o in self.levels[other][p]:
                # Matching would stop here, so neither this nor anything behind
                # it is available to us.
                if self.blocks_self_trade(order, o):
                    return total
                total += o["remaining"]
                if total >= needed:
                    return needed
        return total

    # ---------------------------------------------------------------- commands
    def new_order(self, oid, side, otype, price, qty, out, tif=GTC,
                  post_only=False, participant=0):
        if qty == 0:
            out.append(f"R {oid} zero_quantity")
            return
        # Only limit orders carry a meaningful price; a market order's price
        # field is conventionally 0, which is deliberately out of range.
        if otype == LIMIT and not (MIN_PRICE <= price <= MAX_PRICE):
            out.append(f"R {oid} price_out_of_range")
            return
        if post_only and otype == MARKET:
            out.append(f"R {oid} post_only_market")
            return
        if self.contains(oid):
            out.append(f"R {oid} duplicate_order_id")
            return

        other = SELL if side == BUY else BUY
        order = {"id": oid, "side": side, "type": otype, "price": price,
                 "participant": participant}

        # Refused rather than executed, so it happens before acceptance and
        # before it consumes a sequence number.
        if post_only:
            best = self.best(other)
            if best is not None and self.crosses(order, best):
                out.append(f"R {oid} post_only_would_cross")
                return

        # All or nothing, decided before any trade prints.
        if tif == FOK and self.available(other, order, qty) < qty:
            self.seq += 1
            out.append(f"A {oid} {self.seq}")
            out.append(f"X {oid} {qty} fok_unfillable")
            return

        self.seq += 1
        seq = self.seq
        out.append(f"A {oid} {seq}")
        self._work(oid, side, otype, price, qty, qty, seq, tif, out, participant)

    def _work(self, oid, side, otype, price, total, remaining, seq, tif, out,
              participant=0):
        """Match against the book, then deal with any remainder."""
        other = SELL if side == BUY else BUY
        order = {"id": oid, "side": side, "type": otype, "price": price,
                 "participant": participant}

        while remaining > 0:
            best = self.best(other)
            if best is None:
                break
            if not self.crosses(order, best):
                break
            queue = self.levels[other][best]
            resting = queue[0]                       # oldest at the best price
            # Stop rather than skip, so a participant's own resting order
            # shields everything behind it from that participant.
            if self.blocks_self_trade(order, resting):
                out.append(f"X {oid} {remaining} self_trade")
                return
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
        if otype == LIMIT and tif == GTC:
            self.levels[side].setdefault(price, []).append(
                {"id": oid, "side": side, "type": otype, "price": price,
                 "quantity": total, "remaining": remaining, "seq": seq,
                 "participant": participant})
        else:
            out.append(f"X {oid} {remaining} no_liquidity")

    def locate(self, oid):
        for side_levels in self.levels.values():
            for queue in side_levels.values():
                for o in queue:
                    if o["id"] == oid:
                        return o
        return None

    def modify(self, oid, price, qty, out):
        if qty == 0:
            out.append(f"R {oid} zero_quantity")
            return
        if not (MIN_PRICE <= price <= MAX_PRICE):
            out.append(f"R {oid} price_out_of_range")
            return
        o = self.locate(oid)
        if o is None:
            out.append(f"R {oid} unknown_order")
            return

        filled = o["quantity"] - o["remaining"]
        if qty <= filled:
            unfilled = o["remaining"]
            self.remove(oid)
            self.seq += 1
            out.append(f"X {oid} {unfilled} user")
            return

        new_remaining = qty - filled
        if price == o["price"] and new_remaining <= o["remaining"]:
            o["quantity"] = qty
            o["remaining"] = new_remaining
            kept = o["seq"]                          # unchanged, by design
            self.seq += 1
            out.append(f"M {oid} {qty} {price} {kept} kept")
            return

        side = o["side"]
        owner = o.get("participant", 0)              # a modify never changes it
        self.remove(oid)
        self.seq += 1
        seq = self.seq
        out.append(f"M {oid} {qty} {price} {seq} lost")
        self._work(oid, side, LIMIT, price, qty, new_remaining, seq, GTC, out, owner)

    def cancel(self, oid, out):
        removed = self.remove(oid)
        if removed is None:
            out.append(f"R {oid} unknown_order")
            return
        self.seq += 1
        out.append(f"X {oid} {removed['remaining']} user")


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
        elif parts[0] == "M":
            engine.modify(int(parts[1]), int(parts[2]), int(parts[3]), out)
        elif parts[0] == "N":
            oid, side, otype, price, qty = parts[1:6]
            tif, post_only, participant = GTC, False, 0
            for flag in parts[6:]:
                if flag in (GTC, IOC, FOK):
                    tif = flag
                elif flag == "PO":
                    post_only = True
                elif flag.startswith("P:"):
                    participant = int(flag[2:])
                else:
                    print(f"reference: line {lineno}: unknown flag {flag!r}",
                          file=sys.stderr)
            engine.new_order(int(oid), side, otype, int(price), int(qty), out,
                             tif, post_only, participant)
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
