#!/usr/bin/env python3
"""Generates the PokeExchange explainer document (HTML -> print to PDF).

All chart geometry is computed here rather than hand-authored, and the
statistics charts are driven by an actually-simulated GARCH series with a
fixed seed so the shapes shown are real properties of real numbers.
"""
import numpy as np

# ---------------------------------------------------------------- design tokens
SURFACE = "#fcfcfb"
INK     = "#0b0b0b"
INK2    = "#52514e"
MUTED   = "#898781"
GRID    = "#e1e0d9"
AXIS    = "#c3c2b7"
BID     = "#2a78d6"   # diverging pole 1  (validated)
ASK     = "#e34948"   # diverging pole 2  (validated)
S2      = "#eb6834"   # categorical slot 2
S3      = "#1baf7a"   # categorical slot 3
VIOLET  = "#4a3aa7"

# ------------------------------------------------------------- svg primitives
def bar_r(x0, x1, y, h, r=4):
    """Horizontal bar, baseline at x0, rounded 4px data-end at x1."""
    r = min(r, abs(x1 - x0), h / 2)
    if x1 >= x0:
        return (f"M{x0:.1f},{y:.1f} H{x1-r:.1f} A{r:.1f},{r:.1f} 0 0 1 {x1:.1f},{y+r:.1f} "
                f"V{y+h-r:.1f} A{r:.1f},{r:.1f} 0 0 1 {x1-r:.1f},{y+h:.1f} H{x0:.1f} Z")
    return (f"M{x0:.1f},{y:.1f} H{x1+r:.1f} A{r:.1f},{r:.1f} 0 0 0 {x1:.1f},{y+r:.1f} "
            f"V{y+h-r:.1f} A{r:.1f},{r:.1f} 0 0 0 {x1+r:.1f},{y+h:.1f} H{x0:.1f} Z")

def vbar_r(x, w, y_top, y_base, r=4):
    """Vertical bar, baseline at y_base, rounded 4px data-end at y_top."""
    r = min(r, w / 2, abs(y_base - y_top))
    return (f"M{x:.1f},{y_base:.1f} V{y_top+r:.1f} A{r:.1f},{r:.1f} 0 0 1 {x+r:.1f},{y_top:.1f} "
            f"H{x+w-r:.1f} A{r:.1f},{r:.1f} 0 0 1 {x+w:.1f},{y_top+r:.1f} "
            f"V{y_base:.1f} Z")

def txt(x, y, s, size=11, fill=None, anchor="start", weight=400, mono=False, style=""):
    fill = fill or INK2
    fam = ' font-family="ui-monospace,SFMono-Regular,Menlo,monospace"' if mono else ""
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" '
            f'text-anchor="{anchor}" font-weight="{weight}"{fam} style="{style}">{s}</text>')

def line(x1, y1, x2, y2, stroke=None, w=1, dash=None, cap="butt"):
    d = f' stroke-dasharray="{dash}"' if dash else ""
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{stroke or GRID}" stroke-width="{w}"{d} stroke-linecap="{cap}"/>')

def figure(inner, w, h, title, caption, note=None, cls=""):
    n = f'<p class="fig-note">{note}</p>' if note else ""
    return (f'<figure class="fig {cls}">'
            f'<figcaption class="fig-title">{title}</figcaption>'
            f'<svg viewBox="0 0 {w} {h}" width="100%" role="img" '
            f'aria-label="{title}">{inner}</svg>'
            f'<p class="fig-cap">{caption}</p>{n}</figure>')

def legend(items, x, y, size=11):
    """items: [(label, color)] — always present for >=2 series."""
    out, cx = [], x
    for lab, col in items:
        out.append(f'<rect x="{cx:.1f}" y="{y-8:.1f}" width="9" height="9" rx="2" fill="{col}"/>')
        out.append(txt(cx + 14, y, lab, size=size, fill=INK2))
        cx += 14 + 6.6 * len(lab) + 20
    return "".join(out)

# ------------------------------------------------------- simulated market data
# GARCH(1,1) with Student-t innovations. This genuinely produces the two
# stylized facts we claim: fat tails and volatility clustering.
rng = np.random.default_rng(20260826)
N = 6000
omega, alpha, beta, nu = 2.0e-7, 0.09, 0.89, 5.0
r = np.zeros(N); s2 = np.full(N, omega / (1 - alpha - beta))
z = rng.standard_t(nu, N) / np.sqrt(nu / (nu - 2))
for t in range(1, N):
    s2[t] = omega + alpha * r[t-1]**2 + beta * s2[t-1]
    r[t]  = np.sqrt(s2[t]) * z[t]
r = r[500:]
rz = (r - r.mean()) / r.std()
KURT = float((rz**4).mean())          # 3.0 for a normal distribution

def acf(x, lags):
    x = x - x.mean(); d = float((x * x).sum())
    return [float((x[:-k] * x[k:]).sum() / d) for k in range(1, lags + 1)]

ACF_R, ACF_ABS = acf(rz, 20), acf(np.abs(rz), 20)

# =============================================================== CHART 1: ladder
ASKS = [(103, 400), (102, 150), (101, 50)]          # worst -> best
BIDS = [(99, 200), (98, 500), (97, 900)]            # best  -> worst
QMAX, BX, BW, BH = 900, 100, 250, 22

def ladder_svg():
    s = [f'<rect width="700" height="300" fill="{SURFACE}"/>']
    s.append(legend([("Bids — buyers", BID), ("Asks — sellers", ASK)], 100, 18))
    def row(top, price, qty, col):
        x1 = BX + qty / QMAX * BW
        return ("".join([
            txt(BX - 14, top + 16, str(price), size=13, fill=INK, anchor="end",
                weight=600, mono=True),
            f'<path d="{bar_r(BX, x1, top, BH)}" fill="{col}"/>',
            txt(x1 + 9, top + 16, f"{qty:,}", size=12, fill=INK2, mono=True)]))
    for i, (p, q) in enumerate(ASKS):
        s.append(row(30 + i * 32, p, q, ASK))
    for i, (p, q) in enumerate(BIDS):
        s.append(row(150 + i * 32, p, q, BID))
    # the spread band
    s.append(f'<rect x="{BX-46}" y="116" width="430" height="34" fill="#f0efec" rx="4"/>')
    s.append(txt(BX + 10, 138, "THE SPREAD — nobody is willing to trade in here",
                 size=11, fill=INK2, style="letter-spacing:.02em"))
    s.append(line(BX - 46, 116, BX + 384, 116, AXIS, 1, "3 3"))
    s.append(line(BX - 46, 150, BX + 384, 150, AXIS, 1, "3 3"))
    s.append(line(BX, 24, BX, 244, AXIS, 1))
    # callouts
    co = 415
    for y, lab, col in [(46, "worst ask", MUTED), (110, "BEST ASK 101", ASK),
                        (166, "BEST BID 99", BID), (230, "worst bid", MUTED)]:
        s.append(txt(co, y, lab, size=11, fill=col,
                     weight=600 if col != MUTED else 400, mono=True))
    s.append(txt(co, 128, "spread = 101 − 99 = 2 ticks", size=11, fill=INK2))
    s.append(txt(co, 143, "mid = (101 + 99) / 2 = 100", size=11, fill=INK2))
    s.append(txt(BX, 268, "quantity resting at each price  →", size=10, fill=MUTED))
    s.append(txt(BX - 14, 268, "price", size=10, fill=MUTED, anchor="end"))
    return figure("".join(s), 700, 300,
        "Figure 1 — A limit order book",
        "Every bar is a queue of real people waiting to trade at that price. "
        "Buyers stack up below, sellers above, and the untraded gap between the "
        "two best prices is the spread. Depth grows as you move away from the "
        "middle: a few brave orders near the spread, a wall of patient ones behind.")

# ================================================================ CHART 2: depth
def depth_svg():
    W, H, L, R, T, B = 680, 300, 76, 40, 40, 250
    px = lambda p: L + (p - 96) / 8 * (W - L - R)
    qy = lambda q: B - q / 1600 * (B - T)
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    for q in (0, 400, 800, 1200, 1600):
        s.append(line(L, qy(q), W - R, qy(q), GRID))
        s.append(txt(L - 10, qy(q) + 4, f"{q:,}", size=10, fill=MUTED,
                     anchor="end", mono=True))
    bid = [(99, 200), (98, 700), (97, 1600)]
    ask = [(101, 50), (102, 200), (103, 600)]
    dpath = [f"M{px(99):.1f},{qy(200):.1f}"]
    for i, (p, c) in enumerate(bid):
        nxt = bid[i+1][0] if i + 1 < len(bid) else 96
        dpath.append(f"H{px(nxt):.1f}")
        if i + 1 < len(bid): dpath.append(f"V{qy(bid[i+1][1]):.1f}")
    bfill = " ".join(dpath) + f" V{B:.1f} H{px(99):.1f} Z"
    apath = [f"M{px(101):.1f},{qy(50):.1f}"]
    for i, (p, c) in enumerate(ask):
        nxt = ask[i+1][0] if i + 1 < len(ask) else 104
        apath.append(f"H{px(nxt):.1f}")
        if i + 1 < len(ask): apath.append(f"V{qy(ask[i+1][1]):.1f}")
    afill = " ".join(apath) + f" V{B:.1f} H{px(101):.1f} Z"
    s.append(f'<path d="{bfill}" fill="{BID}" fill-opacity="0.16"/>')
    s.append(f'<path d="{afill}" fill="{ASK}" fill-opacity="0.16"/>')
    s.append(f'<path d="{" ".join(dpath)}" fill="none" stroke="{BID}" stroke-width="2"/>')
    s.append(f'<path d="{" ".join(apath)}" fill="none" stroke="{ASK}" stroke-width="2"/>')
    s.append(line(L, B, W - R, B, AXIS, 1))
    for p in range(96, 105):
        s.append(txt(px(p), B + 18, str(p), size=10, fill=MUTED, anchor="middle", mono=True))
    s.append(f'<rect x="{px(99):.1f}" y="{T}" width="{px(101)-px(99):.1f}" '
             f'height="{B-T}" fill="#f0efec" fill-opacity="0.8"/>')
    s.append(txt((px(99)+px(101))/2, T - 8, "spread", size=10, fill=MUTED, anchor="middle"))
    s.append(txt(px(97.4), qy(1600) - 10, "1,600 total bid", size=11, fill=BID, weight=600))
    s.append(txt(px(102.6), qy(600) - 10, "600 total ask", size=11, fill=ASK, weight=600))
    s.append(legend([("Cumulative bids", BID), ("Cumulative asks", ASK)], L, 22))
    s.append(txt(L - 10, T - 18, "qty", size=10, fill=MUTED, anchor="end"))
    s.append(txt(W - R, B + 34, "price →", size=10, fill=MUTED, anchor="end"))
    return figure("".join(s), W, H,
        "Figure 2 — The same book as a depth curve",
        "Each step answers one question: “if I bought/sold everything up to this "
        "price, how much would I get?” The steeper the wall, the harder the book "
        "is to push. A thin, flat curve — a Legendary with almost no float — means "
        "even a small order rips through several price levels.")

# ============================================================== CHART 3: a sweep
def sweep_svg():
    W, H = 700, 320
    panels = [
        ("① Book before", [(103, 400, 400), (102, 150, 150), (101, 50, 50)], []),
        ("② Fills 50 @ 101", [(103, 400, 400), (102, 150, 150), (101, 0, 50)],
         ["50 @ 101"]),
        ("③ Fills 130 @ 102", [(103, 400, 400), (102, 20, 150), (101, 0, 50)],
         ["50 @ 101", "130 @ 102"]),
    ]
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    s.append(txt(20, 22, "A market order to BUY 180 walks up the ask side, "
                 "oldest order at each price first", size=12, fill=INK, weight=600))
    for pi, (title, rows, prints) in enumerate(panels):
        ox = 20 + pi * 228
        s.append(txt(ox, 50, title, size=11, fill=INK, weight=600))
        for ri, (price, rem, orig) in enumerate(rows):
            y = 62 + ri * 30
            s.append(txt(ox + 26, y + 15, str(price), size=12, fill=INK,
                         anchor="end", weight=600, mono=True))
            gx0, gw = ox + 34, 120
            s.append(f'<path d="{bar_r(gx0, gx0 + orig/400*gw, y, 20)}" '
                     f'fill="{ASK}" fill-opacity="0.18"/>')
            if rem:
                s.append(f'<path d="{bar_r(gx0, gx0 + rem/400*gw, y, 20)}" fill="{ASK}"/>')
            lab = f"{rem:,}" if rem else "gone"
            s.append(txt(gx0 + orig/400*gw + 8, y + 15, lab, size=11,
                         fill=INK2 if rem else MUTED, mono=True))
        s.append(f'<rect x="{ox}" y="158" width="200" height="{28+20*max(1,len(prints))}" '
                 f'rx="6" fill="#f0efec"/>')
        s.append(txt(ox + 10, 176, "PRINTS ON THE TAPE", size=9, fill=MUTED,
                     style="letter-spacing:.08em"))
        if not prints:
            s.append(txt(ox + 10, 196, "—", size=11, fill=MUTED, mono=True))
        for i, p in enumerate(prints):
            s.append(txt(ox + 10, 196 + i * 20, p, size=11, fill=INK, mono=True, weight=600))
        if pi < 2:
            s.append(txt(ox + 212, 110, "→", size=20, fill=MUTED))
    s.append(f'<rect x="20" y="252" width="660" height="52" rx="6" fill="#eef4fc"/>')
    s.append(txt(34, 272, "Result: 180 bought. Average price paid = "
                 "(50×101 + 130×102) / 180 = 101.72", size=11, fill=INK, weight=600))
    s.append(txt(34, 291, "The buyer wanted 101 and got 101.72. That 0.72 is slippage — "
                 "the cost of being in a hurry.", size=11, fill=INK2))
    return figure("".join(s), W, H,
        "Figure 3 — Matching, one fill at a time",
        "This is the entire engine. The incoming order (the taker) is matched "
        "against resting orders (the makers) in strict price-then-time order, "
        "printing a trade for each pairing, until it is filled or the book runs out.")

# ====================================================== CHART 4: memory hierarchy
def memory_svg():
    W, H, L = 700, 236, 132
    items = [("L1 cache", 1.0, S3), ("L2 cache", 4.0, S3),
             ("L3 cache", 20.0, S2), ("Main memory (DRAM)", 100.0, ASK)]
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    s.append(txt(L, 24, "Time to fetch one piece of data, if it lives in…",
                 size=11, fill=INK2))
    for i, (lab, ns, col) in enumerate(items):
        y = 40 + i * 42
        s.append(txt(L - 12, y + 17, lab, size=11, fill=INK, anchor="end", weight=600))
        w = max(3.0, ns / 100 * 440)
        s.append(f'<path d="{bar_r(L, L + w, y, 24)}" fill="{col}"/>')
        s.append(txt(L + w + 10, y + 17, f"≈{ns:g} ns", size=12, fill=INK,
                     mono=True, weight=600))
        if i == 0:
            s.append(txt(L + w + 66, y + 17, "(you can barely see this bar — that's the point)",
                         size=10, fill=MUTED))
    s.append(line(L, 36, L, 40 + 4 * 42 - 8, AXIS, 1))
    s.append(txt(L, 226, "A cache miss costs ~100× a cache hit. This single fact "
                 "is why v1–v3 exist.", size=11, fill=INK, weight=600))
    return figure("".join(s), W, H,
        "Figure 4 — Why memory layout is the whole game",
        "Modern CPUs are starved, not slow. A processor can do a hundred "
        "arithmetic operations in the time it takes to wait for one value that "
        "isn't in cache. Order-of-magnitude figures for a typical desktop CPU.")

# ======================================================= CHART 5: pointer chase
def pointer_svg():
    W, H, CELL, NC, L = 700, 300, 14, 40, 70
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    def strip(y, hot, label, sub, col, misses):
        o = [txt(L, y - 42, label, size=12, fill=INK, weight=600, mono=True),
             txt(L, y - 26, sub, size=10, fill=MUTED)]
        for c in range(NC):
            fill = col if c in hot else "#f0efec"
            o.append(f'<rect x="{L+c*CELL:.1f}" y="{y}" width="{CELL-2}" '
                     f'height="20" rx="2" fill="{fill}"/>')
        for k in range(0, NC + 1, 8):   # cache-line boundaries
            o.append(line(L + k * CELL - 1, y - 5, L + k * CELL - 1, y + 25, AXIS, 1, "2 2"))
        order = hot
        for i in range(len(order) - 1):
            x1 = L + order[i] * CELL + CELL / 2 - 1
            x2 = L + order[i+1] * CELL + CELL / 2 - 1
            o.append(f'<path d="M{x1:.1f},{y} Q{(x1+x2)/2:.1f},{y-24:.1f} {x2:.1f},{y}" '
                     f'fill="none" stroke="{col}" stroke-width="1.5" opacity="0.75"/>')
        o.append(txt(L + NC * CELL + 14, y + 15, misses, size=11, fill=INK,
                     weight=600, mono=True))
        return "".join(o)
    s.append(strip(84, [3, 27, 11, 34, 6, 19],
                   "v0:  std::map&lt;Price, deque&lt;Order&gt;&gt;",
                   "each node landed wherever the allocator had room — walking the "
                   "book jumps all over RAM", ASK, "6 misses"))
    s.append(strip(210, [12, 13, 14, 15, 16, 17],
                   "v1:  Order levels[MAX_TICKS]",
                   "price IS the array index — neighbouring prices are neighbouring "
                   "bytes, and the prefetcher sees you coming", S3, "1 miss"))
    s.append(txt(L, 268, "Same algorithm. Same big-O. Roughly 6× fewer trips to memory.",
                 size=11, fill=INK, weight=600))
    s.append(txt(L, 284, "Dashed boxes are 64-byte cache lines: the CPU always "
                 "fetches a whole line, never one value.", size=10, fill=MUTED))
    return figure("".join(s), W, H,
        "Figure 5 — The same book, laid out two ways",
        "A tree stores its nodes wherever malloc found space, so traversing it is a "
        "pointer chase: fetch, wait, fetch, wait. An array indexed by price is "
        "contiguous, so the hardware prefetcher loads the next levels before you ask.")

# ========================================================= CHART 6: latency hist
def lat_hist_svg():
    W, H, L, R = 700, 392, 66, 24
    g = np.random.default_rng(7)
    v0 = np.exp(g.normal(np.log(690), 0.46, 60000))
    spike = g.random(60000) < 0.015          # occasional allocation / rehash stall
    v0[spike] *= g.uniform(3, 9, int(spike.sum()))
    v3 = np.exp(g.normal(np.log(88), 0.28, 60000))
    edges = np.logspace(np.log10(20), np.log10(20000), 56)
    lx = lambda v: L + (np.log10(v) - np.log10(20)) / (np.log10(20000) - np.log10(20)) * (W - L - R)
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    for pi, (name, data, col) in enumerate([("v0 \u2014 std::map", v0, S2),
                                            ("v3 \u2014 ladder + pool + hash", v3, BID)]):
        top = 62 + pi * 188
        base = top + 104
        h, _ = np.histogram(data, edges)
        hmax = h.max()
        s.append(txt(L, top - 46, name, size=12, fill=INK, weight=600, mono=True))
        for i in range(len(h)):
            if not h[i]: continue
            x0, x1 = lx(edges[i]), lx(edges[i+1])
            bh = h[i] / hmax * (base - top)
            s.append(f'<path d="{vbar_r(x0 + 1, max(1.2, x1-x0-2), base - bh, base, 2)}" fill="{col}"/>')
        s.append(line(L, base, W - R, base, AXIS, 1))
        # percentile markers, staggered so tight distributions do not collide
        for (q, lab), dy in zip([(50, "p50"), (99, "p99"), (99.9, "p99.9")], (32, 20, 8)):
            v = float(np.percentile(data, q)); x = lx(v)
            s.append(line(x, top - dy + 3, x, base, INK, 1, "3 3"))
            s.append(txt(x + 5, top - dy + 4, f"{lab}  {v:,.0f}ns", size=9.5, fill=INK,
                         mono=True, weight=600))
        for e in (100, 1000, 10000):
            s.append(txt(lx(e), base + 15, f"{e:,}", size=10, fill=MUTED,
                         anchor="middle", mono=True))
        s.append(txt(L - 10, base, "0", size=10, fill=MUTED, anchor="end", mono=True))
        s.append(txt(L - 10, top + 8, "count", size=10, fill=MUTED, anchor="end"))
    s.append(txt(W - R, 386, "nanoseconds per order, log scale \u2192", size=10,
                 fill=MUTED, anchor="end"))
    return figure("".join(s), W, H,
        "Figure 6 \u2014 What a latency measurement looks like",
        "Two panels, one shared axis \u2014 never two y-scales on one chart. The whole "
        "distribution matters, not the average: the long right tail is where "
        "money is lost, which is why the numbers you quote are p99 and p99.9.",
        note="ILLUSTRATIVE SHAPE ONLY \u2014 simulated to show the form of the result. "
             "We have not built the engine yet; producing these numbers honestly is "
             "the entire point of sub-project C.")

# ==================================================== CHART 7: return distribution
def ret_dist_svg():
    W, H, L, R, T, B = 680, 300, 64, 30, 46, 244
    edges = np.linspace(-8, 8, 65)
    h, _ = np.histogram(rz, edges)
    n, bw = len(rz), edges[1] - edges[0]
    ctr = (edges[:-1] + edges[1:]) / 2
    expect = n * bw * np.exp(-ctr**2 / 2) / np.sqrt(2 * np.pi)
    ymax = np.log10(h.max() + 1)
    xs = lambda v: L + (v + 8) / 16 * (W - L - R)
    ys = lambda c: B - np.log10(c + 1) / ymax * (B - T)
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    for d in (1, 10, 100, 1000):
        s.append(line(L, ys(d), W - R, ys(d), GRID))
        s.append(txt(L - 9, ys(d) + 4, f"{d:,}", size=10, fill=MUTED, anchor="end", mono=True))
    for i in range(len(h)):
        if not h[i]: continue
        s.append(f'<path d="{vbar_r(xs(edges[i])+1, max(1.2, xs(edges[i+1])-xs(edges[i])-2), ys(h[i]), B, 2)}" fill="{BID}"/>')
    pts = " ".join(f"{xs(c):.1f},{ys(e):.1f}" for c, e in zip(ctr, expect) if e > 0.02)
    s.append(f'<polyline points="{pts}" fill="none" stroke="{INK}" stroke-width="2" stroke-dasharray="5 3"/>')
    s.append(line(L, B, W - R, B, AXIS, 1))
    for v in range(-8, 9, 2):
        s.append(txt(xs(v), B + 16, f"{v:+d}σ" if v else "0", size=10, fill=MUTED,
                     anchor="middle", mono=True))
    for x, lab in [(xs(-6.4), "fat tail"), (xs(6.4), "fat tail")]:
        s.append(txt(x, T + 96, lab, size=10, fill=ASK, anchor="middle", weight=600))
        s.append(f'<path d="M{x:.1f},{T+102} L{x:.1f},{T+126}" stroke="{ASK}" stroke-width="1.5"/>')
    s.append(legend([("Simulated returns", BID)], L, 24))
    s.append(f'<line x1="{L+180}" y1="20" x2="{L+206}" y2="20" stroke="{INK}" '
             f'stroke-width="2" stroke-dasharray="5 3"/>')
    s.append(txt(L + 212, 24, "Normal distribution (same σ)", size=11, fill=INK2))
    s.append(txt(L - 9, T - 18, "count", size=10, fill=MUTED, anchor="end"))
    s.append(txt(W - R, 288, "return, in standard deviations →", size=10, fill=MUTED, anchor="end"))
    return figure("".join(s), W, H,
        "Figure 7 — Real markets are not bell curves",
        f"Log vertical scale. The dashed line is what a Normal distribution "
        f"predicts; the bars are what actually happens. Beyond about ±4σ the "
        f"Normal says “never”, and reality says “Tuesday”. Measured kurtosis here "
        f"is {KURT:.2f} against 3.00 for a Normal.",
        note="Computed from a simulated GARCH(1,1) series with Student-t shocks — "
             "the same statistical test we will run against our engine's real output.")

# ================================================================== CHART 8: ACF
def acf_svg():
    W, H = 690, 278
    band = 1.96 / np.sqrt(len(rz))
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    for pi, (data, title, col, sub) in enumerate([
            (ACF_R, "Autocorrelation of returns", BID, "≈ 0 — direction is unpredictable"),
            (ACF_ABS, "Autocorrelation of |returns|", S2, "&gt; 0 for many lags — size is predictable")]):
        L = 56 + pi * 348; T, B, PW = 60, 200, 250
        zero = (T + B) / 2
        ys = lambda v: zero - v / 0.30 * (B - zero)
        s.append(txt(L, 30, title, size=11.5, fill=INK, weight=600))
        s.append(txt(L, 46, sub, size=10, fill=INK2))
        s.append(f'<rect x="{L}" y="{ys(band):.1f}" width="{PW}" '
                 f'height="{ys(-band)-ys(band):.1f}" fill="#f0efec"/>')
        for v in (0.2, 0.0, -0.2):
            s.append(line(L, ys(v), L + PW, ys(v), GRID if v else AXIS))
            s.append(txt(L - 8, ys(v) + 4, f"{v:+.1f}" if v else "0", size=9.5,
                         fill=MUTED, anchor="end", mono=True))
        bwid = PW / len(data)
        for i, v in enumerate(data):
            x = L + i * bwid
            y0, y1 = (ys(v), ys(0)) if v >= 0 else (ys(0), ys(v))
            s.append(f'<path d="{vbar_r(x+1, bwid-2, y0, y1, 1.5)}" fill="{col}"/>')
        for lg in (1, 5, 10, 15, 20):
            s.append(txt(L + (lg - 0.5) * bwid, B + 32, str(lg), size=9.5,
                         fill=MUTED, anchor="middle", mono=True))
        s.append(txt(L + PW / 2, B + 46, "lag (trades)", size=10, fill=MUTED, anchor="middle"))
    s.append(txt(56, 268, "Grey band = the range where a value is statistically "
                 "indistinguishable from zero.", size=10, fill=MUTED))
    return figure("".join(s), W, H,
        "Figure 8 — Volatility clusters; direction does not",
        "The left panel is why you cannot make money by guessing the next tick. The "
        "right panel is why you can still make money selling insurance against "
        "volatility: quiet periods follow quiet periods, storms follow storms.")

# ============================================================== CHART 9: impact
def impact_svg():
    W, H, L, R, T, B = 660, 300, 74, 34, 46, 240
    Y, XMAX = 0.7, 0.20
    xs = lambda v: L + v / XMAX * (W - L - R)
    ys = lambda v: B - v / 0.36 * (B - T)
    g = np.random.default_rng(3)
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    for v in (0.0, 0.1, 0.2, 0.3):
        s.append(line(L, ys(v), W - R, ys(v), GRID if v else AXIS))
        s.append(txt(L - 9, ys(v) + 4, f"{v:.1f}", size=10, fill=MUTED, anchor="end", mono=True))
    lin = f"M{xs(0):.1f},{ys(0):.1f} L{xs(XMAX):.1f},{ys(Y*np.sqrt(XMAX)):.1f}"
    s.append(f'<path d="{lin}" fill="none" stroke="{MUTED}" stroke-width="2" stroke-dasharray="5 3"/>')
    curve = " ".join(f"{xs(x):.1f},{ys(Y*np.sqrt(x)):.1f}" for x in np.linspace(0, XMAX, 120))
    s.append(f'<polyline points="{curve}" fill="none" stroke="{BID}" stroke-width="2"/>')
    for x in g.uniform(0.004, XMAX, 34):
        y = Y * np.sqrt(x) * g.normal(1, 0.11)
        s.append(f'<circle cx="{xs(x):.1f}" cy="{ys(max(y,0)):.1f}" r="3.6" '
                 f'fill="{BID}" fill-opacity="0.45" stroke="{SURFACE}" stroke-width="2"/>')
    for x, lab in [(0.02, "4× the size…"), (0.08, "…only 2× the cost")]:
        y = Y * np.sqrt(x)
        s.append(line(xs(x), ys(y), xs(x), B, INK, 1, "2 2"))
        s.append(line(L, ys(y), xs(x), ys(y), INK, 1, "2 2"))
        s.append(f'<circle cx="{xs(x):.1f}" cy="{ys(y):.1f}" r="5" fill="{INK}"/>')
        s.append(txt(xs(x) + 10, ys(y) - 8, f"{lab}  ({y:.2f}σ)", size=11,
                     fill=INK, weight=600))
    s.append(line(L, B, W - R, B, AXIS, 1))
    for v in (0, 0.05, 0.10, 0.15, 0.20):
        s.append(txt(xs(v), B + 16, f"{v*100:g}%", size=10, fill=MUTED,
                     anchor="middle", mono=True))
    s.append(legend([("Square-root law:  impact = Y·σ·√(Q/V)", BID)], L, 24))
    s.append(f'<line x1="{L+300}" y1="20" x2="{L+326}" y2="20" stroke="{MUTED}" '
             f'stroke-width="2" stroke-dasharray="5 3"/>')
    s.append(txt(L + 332, 24, "if impact were proportional to size", size=11, fill=INK2))
    s.append(txt(L - 9, T - 18, "impact (σ)", size=10, fill=MUTED, anchor="end"))
    s.append(txt(W - R, 284, "order size as % of daily volume →", size=10,
                 fill=MUTED, anchor="end"))
    return figure("".join(s), W, H,
        "Figure 9 — Trading costs grow like a square root, not a straight line",
        "One of the most robust empirical laws in finance, found across every "
        "market anyone has checked. It is why large orders are sliced up and fed in "
        "slowly, and it is a law our simulated market either reproduces or does not.",
        note="Curve is the theoretical law; dots are simulated observations scattered "
             "around it, showing the noise a real measurement has.")

# ================================================= CHART 10: Avellaneda–Stoikov
def as_svg():
    W, H = 760, 276
    ys = lambda p: 272 - (p - 96) * 30          # 96..102 maps to 272..92
    MID, SKEW, HALF = 100.0, 1.2, 0.6           # illustrative magnitudes
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    # the market mid, labelled clear of every column
    s.append(line(60, ys(MID), 700, ys(MID), INK, 1.5, "6 3"))
    s.append(txt(W - 8, ys(MID) - 7, "market mid  s = 100", size=10.5, fill=INK,
                 anchor="end", weight=600))
    cols = [(-10, "SHORT  q = \u2212 10", "wants to BUY back"),
            (0,   "FLAT   q = 0",         "no preference"),
            (+10, "LONG   q = + 10",      "wants to SELL down")]
    for ci, (q, title, mood) in enumerate(cols):
        cx = 140 + ci * 200
        r = MID - (q / 10.0) * SKEW
        bid, ask = r - HALF, r + HALF
        s.append(txt(cx, 42, title, size=11.5, fill=INK, weight=600,
                     anchor="middle", mono=True))
        s.append(txt(cx, 58, mood, size=10, fill=MUTED, anchor="middle"))
        s.append(line(cx, 76, cx, 214, GRID, 1))
        # reservation price, labelled above its own line
        s.append(line(cx - 56, ys(r), cx + 56, ys(r), VIOLET, 2))
        s.append(f'<rect x="{cx-31}" y="{ys(r)-8:.1f}" width="62" height="16" '
                 f'rx="3" fill="{SURFACE}"/>')
        s.append(txt(cx, ys(r) + 4, f"r = {r:.1f}", size=10.5, fill=VIOLET,
                     anchor="middle", weight=600, mono=True))
        # the two quotes
        for p, col, lab in [(ask, ASK, "ask"), (bid, BID, "bid")]:
            s.append(f'<path d="{bar_r(cx - 42, cx + 42, ys(p) - 5, 10, 3)}" fill="{col}"/>')
            s.append(txt(cx - 48, ys(p) + 4, lab, size=10, fill=col, anchor="end",
                         weight=600, mono=True))
        # spread bracket, right of the bars
        bx = cx + 56
        s.append(line(bx, ys(ask), bx, ys(bid), INK2, 1))
        s.append(line(bx - 3, ys(ask), bx + 3, ys(ask), INK2, 1))
        s.append(line(bx - 3, ys(bid), bx + 3, ys(bid), INK2, 1))
        s.append(txt(bx + 7, (ys(ask) + ys(bid)) / 2 + 4, "\u03b4", size=12,
                     fill=INK2, mono=True))
        # the inventory skew, as an arrow from the mid down/up to r
        if q:
            ax = cx - 78
            y0, y1 = ys(MID), ys(r)
            d = 6 if y1 > y0 else -6
            s.append(line(ax, y0, ax, y1, VIOLET, 1.5))
            s.append(f'<path d="M{ax-4},{y1-d} L{ax},{y1} L{ax+4},{y1-d}" '
                     f'fill="none" stroke="{VIOLET}" stroke-width="1.5"/>')
    s.append(f'<rect x="60" y="226" width="{W-120}" height="38" rx="6" fill="#f0efec"/>')
    s.append(txt(74, 242, "Holding inventory is risky, so the maker shifts BOTH "
                 "quotes away from it.", size=11, fill=INK, weight=600))
    s.append(txt(74, 257, "The purple arrow is the inventory skew. Long \u2192 cheaper "
                 "offers, stingier bids, so the position works itself off.",
                 size=10.5, fill=INK2))
    return figure("".join(s), W, H,
        "Figure 10 \u2014 Avellaneda\u2013Stoikov: how inventory bends your quotes",
        "The reservation price r is the price at which the maker is genuinely "
        "indifferent \u2014 it is not the market mid, it is the mid adjusted for the "
        "risk they are already carrying. Quotes sit symmetrically around r, so the "
        "skew is what makes the position self-liquidating.",
        note="Skew and spread magnitudes chosen for legibility, not calibrated.")

# ============================================================ CHART 11: coint
def coint_svg():
    n = 600
    g = np.random.default_rng(11)
    common = np.cumsum(g.normal(0, 1, n)) * 0.35
    A = 20 + common + g.normal(0, 0.12, n)
    B = 60 + 3 * common + g.normal(0, 0.30, n)
    t = np.arange(n)
    bump = np.where((t >= 280) & (t < 400), 2.3 * np.sin(np.pi * (t - 280) / 120), 0.0)
    B = B + bump
    spread = B - 3 * A
    z = (spread - spread.mean()) / spread.std()
    W, H, L, R = 690, 380, 62, 26
    xs = lambda i: L + i / (n - 1) * (W - L - R)
    s = [f'<rect width="{W}" height="{H}" fill="{SURFACE}"/>']
    # panel 1 — indexed prices (one axis; never two scales)
    T1, B1 = 46, 178
    ai, bi = A / A[0] * 100, B / B[0] * 100
    lo, hi = min(ai.min(), bi.min()), max(ai.max(), bi.max())
    y1 = lambda v: B1 - (v - lo) / (hi - lo) * (B1 - T1)
    for frac in (0, .5, 1):
        v = lo + frac * (hi - lo)
        s.append(line(L, y1(v), W - R, y1(v), GRID))
        s.append(txt(L - 8, y1(v) + 4, f"{v:.0f}", size=9.5, fill=MUTED, anchor="end", mono=True))
    for series, col, lab in [(ai, BID, "Charmander"), (bi, S2, "Charizard")]:
        pts = " ".join(f"{xs(i):.1f},{y1(v):.1f}" for i, v in enumerate(series))
        s.append(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="1.8"/>')
    s.append(legend([("Charmander", BID), ("Charizard", S2)], L, 28))
    s.append(txt(L - 8, T1 - 16, "indexed", size=9.5, fill=MUTED, anchor="end"))
    s.append(txt(L, 40, "Both indexed to 100 at the start, so one shared scale.",
                 size=9.5, fill=MUTED))
    # panel 2 — z-score of the spread
    T2, B2 = 232, 340
    zmax = 3.6
    y2 = lambda v: (T2 + B2) / 2 - v / zmax * (B2 - T2) / 2
    s.append(f'<rect x="{L}" y="{y2(2):.1f}" width="{W-L-R}" '
             f'height="{y2(-2)-y2(2):.1f}" fill="#f0efec"/>')
    for v in (2, 0, -2):
        s.append(line(L, y2(v), W - R, y2(v), AXIS if v == 0 else GRID,
                      1, None if v == 0 else "4 3"))
        s.append(txt(L - 8, y2(v) + 4, f"{v:+.0f}σ" if v else "0", size=9.5,
                     fill=MUTED, anchor="end", mono=True))
    over = np.abs(z) > 2
    i = 0
    while i < n:
        if over[i]:
            j = i
            while j < n and over[j]: j += 1
            s.append(f'<rect x="{xs(i):.1f}" y="{T2}" width="{max(1,xs(j-1)-xs(i)):.1f}" '
                     f'height="{B2-T2}" fill="{ASK}" fill-opacity="0.10"/>')
            i = j
        else: i += 1
    pts = " ".join(f"{xs(i):.1f},{y2(v):.1f}" for i, v in enumerate(z))
    s.append(f'<polyline points="{pts}" fill="none" stroke="{VIOLET}" stroke-width="1.6"/>')
    s.append(txt(L, 216, "Spread  =  Charizard − 3 × Charmander,  as a z-score",
                 size=11.5, fill=INK, weight=600))
    k = int(np.argmax(np.abs(z)))
    s.append(txt(xs(k) + 8, y2(z[k]) - 10, "arb bot sells Charizard,", size=10,
                 fill=ASK, weight=600))
    s.append(txt(xs(k) + 8, y2(z[k]) + 3, "buys Charmander", size=10, fill=ASK, weight=600))
    s.append(txt(W - R, 372, "time →", size=10, fill=MUTED, anchor="end"))
    return figure("".join(s), W, H,
        "Figure 11 — Cointegration: the evolution line has to hang together",
        "Individually each price wanders unpredictably. Their difference does not — "
        "it is tethered. When the tether stretches past ±2σ (shaded), the "
        "arbitrageur bets it snaps back. That bet is not a prediction about "
        "Charizard; it is a prediction about the relationship.")

# ================================================================== STYLESHEET
CSS = """
@page { size: letter; margin: 15mm 14mm 16mm 14mm; }
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body {
  margin: 0; background: #f9f9f7; color: #0b0b0b;
  font-family: system-ui, -apple-system, "Segoe UI", sans-serif;
  font-size: 10.6pt; line-height: 1.58; text-rendering: optimizeLegibility;
}
.sheet { max-width: 186mm; margin: 0 auto; padding: 0 2mm; }
p { margin: 0 0 .78em; }
b, strong { font-weight: 650; }
code, .mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: .93em; }
code { background: #eeede8; padding: .1em .34em; border-radius: 3px; }
a { color: #2a78d6; }

/* ---- cover ---- */
.cover { height: 246mm; display: flex; flex-direction: column; justify-content: center; }
.cover .eyebrow { font-size: 10pt; letter-spacing: .22em; text-transform: uppercase;
  color: #898781; margin-bottom: 10mm; }
.cover h1 { font-size: 40pt; line-height: 1.02; margin: 0 0 6mm; letter-spacing: -.022em;
  font-weight: 680; }
.cover h1 .sub { display: block; font-size: 17pt; font-weight: 450; color: #52514e;
  letter-spacing: -.008em; margin-top: 5mm; }
.cover .rule { height: 5px; width: 74mm; background: #2a78d6; border-radius: 3px; margin: 8mm 0; }
.cover .lede { font-size: 12.4pt; line-height: 1.55; max-width: 132mm; color: #2a2a28; }
.cover .meta { margin-top: auto; padding-top: 12mm; font-size: 9.6pt; color: #898781;
  display: flex; gap: 10mm; flex-wrap: wrap; }

/* ---- structure ---- */
h2.part { break-before: page; page-break-before: always;
  font-size: 20pt; font-weight: 660; letter-spacing: -.014em;
  margin: 0 0 1mm; padding-top: 2mm; line-height: 1.15; }
h2.part .num { display: block; font-size: 9.4pt; letter-spacing: .2em; font-weight: 600;
  color: #2a78d6; text-transform: uppercase; margin-bottom: 2.5mm; }
h2.part + .standfirst { font-size: 11.4pt; color: #52514e; margin: 0 0 7mm;
  padding-bottom: 5mm; border-bottom: 2px solid #e1e0d9; max-width: 150mm; }
h3 { font-size: 13pt; font-weight: 640; margin: 8mm 0 2.4mm; letter-spacing: -.008em;
  break-after: avoid; page-break-after: avoid; }
h4 { font-size: 10.9pt; font-weight: 640; margin: 6mm 0 1.6mm; color: #2a2a28;
  break-after: avoid; page-break-after: avoid; }

/* ---- figures ---- */
.fig { margin: 6mm 0 7mm; padding: 4.5mm 5mm 3.5mm; background: #fcfcfb;
  border: 1px solid rgba(11,11,11,.10); border-radius: 8px;
  break-inside: avoid; page-break-inside: avoid; }
.fig-title { font-size: 10.3pt; font-weight: 660; margin-bottom: 3mm; color: #0b0b0b; }
.fig svg { display: block; }
.fig-cap { font-size: 9.5pt; line-height: 1.5; color: #52514e; margin: 3mm 0 0; }
.fig-note { font-size: 8.7pt; line-height: 1.45; color: #898781; margin: 1.8mm 0 0;
  font-style: italic; }

/* ---- callouts ---- */
.key, .aside, .flag {
  border-radius: 8px; padding: 4mm 5mm; margin: 5mm 0;
  break-inside: avoid; page-break-inside: avoid; font-size: 10.2pt; }
.key { background: #eef4fc; border-left: 4px solid #2a78d6; }
.aside { background: #f2f1ed; border-left: 4px solid #c3c2b7; }
.flag { background: #fdf0ec; border-left: 4px solid #e34948; }
.key .lab, .aside .lab, .flag .lab {
  display: block; font-size: 8.7pt; letter-spacing: .13em; text-transform: uppercase;
  font-weight: 700; margin-bottom: 1.8mm; }
.key .lab { color: #1c5cab; } .aside .lab { color: #6f6d67; } .flag .lab { color: #b3302f; }
.key p:last-child, .aside p:last-child, .flag p:last-child { margin-bottom: 0; }

/* ---- equations ---- */
.eq { background: #f4f3ef; border-radius: 8px; padding: 4.5mm 5mm; margin: 4mm 0;
  text-align: center; font-size: 12.4pt; line-height: 1.85;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  break-inside: avoid; page-break-inside: avoid; }
.eq .where { display: block; margin-top: 3mm; font-size: 9.3pt; color: #52514e;
  text-align: left; font-family: system-ui, sans-serif; line-height: 1.55; }
.frac { display: inline-flex; flex-direction: column; vertical-align: middle;
  text-align: center; margin: 0 .18em; font-size: .88em; }
.frac .n { border-bottom: 1.4px solid currentColor; padding: 0 .34em; }
.frac .d { padding: 0 .34em; }
.term { display: inline-block; border-bottom: 2px solid #2a78d6; padding-bottom: 1px; }
.rad { display: inline-block; border-top: 1.4px solid currentColor;
  padding: 2px .3em 0; margin-left: -1px; }

/* ---- tables ---- */
table { width: 100%; border-collapse: collapse; margin: 4.5mm 0; font-size: 9.7pt;
  break-inside: avoid; page-break-inside: avoid; }
th { text-align: left; font-weight: 660; font-size: 8.6pt; letter-spacing: .09em;
  text-transform: uppercase; color: #52514e; border-bottom: 1.6px solid #c3c2b7;
  padding: 2mm 3mm 1.6mm; }
td { padding: 2.2mm 3mm; border-bottom: 1px solid #e8e7e1; vertical-align: top; }
tr:last-child td { border-bottom: none; }
td.mono, th.mono { font-family: ui-monospace, Menlo, monospace; }
td.num { text-align: right; font-variant-numeric: tabular-nums;
  font-family: ui-monospace, Menlo, monospace; }

/* ---- lists ---- */
ul, ol { margin: 0 0 .8em; padding-left: 5.5mm; }
li { margin-bottom: .34em; }
dl.gloss { margin: 0; }
dl.gloss dt { font-weight: 660; margin-top: 3.4mm; font-size: 10.2pt; }
dl.gloss dd { margin: .5mm 0 0; color: #2a2a28; font-size: 10pt; }

/* ---- misc ---- */
.two { display: grid; grid-template-columns: 1fr 1fr; gap: 0 8mm; }
.pill { display: inline-block; background: #2a78d6; color: #fff; font-size: 8.4pt;
  font-weight: 650; letter-spacing: .06em; padding: .9mm 2.4mm; border-radius: 99px;
  text-transform: uppercase; vertical-align: 1.5px; }
.pill.grey { background: #898781; }
pre { background: #f4f3ef; border-radius: 8px; padding: 4mm 5mm; margin: 4mm 0;
  font-size: 9.1pt; line-height: 1.55; overflow: visible; white-space: pre-wrap;
  break-inside: avoid; page-break-inside: avoid;
  font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
pre .c { color: #898781; }
.tocbox { background: #fcfcfb; border: 1px solid rgba(11,11,11,.10); border-radius: 8px;
  padding: 5mm 6mm; margin: 6mm 0 0; }
.tocbox ol { counter-reset: t; list-style: none; padding: 0; margin: 0; }
.tocbox li { display: flex; gap: 3mm; padding: 1.5mm 0; border-bottom: 1px solid #eeede8;
  font-size: 10pt; }
.tocbox li:last-child { border-bottom: none; }
.tocbox li b { min-width: 22mm; color: #2a78d6; font-size: 8.8pt; letter-spacing: .1em;
  text-transform: uppercase; padding-top: .7mm; }
"""

def frac(n, d):
    return f'<span class="frac"><span class="n">{n}</span><span class="d">{d}</span></span>'

DOC = []
def add(*x): DOC.extend(x)

# ------------------------------------------------------------------------ cover
add("""
<section class="cover">
  <div class="eyebrow">Design primer &middot; sub-project A</div>
  <h1>Limit Order Book<br>&amp; Matching Engine
    <span class="sub">Building an electronic exchange from first principles &mdash;
    and understanding every part of why</span></h1>
  <div class="rule"></div>
  <p class="lede">This document exists so that you can explain this project to
  somebody else. It assumes you know how to program and nothing whatsoever about
  finance. Every term is defined the first time it appears, every formula is taken
  apart piece by piece, and nothing is asserted without a reason.</p>
  <div class="meta">
    <span>Internal codename &middot; Pok&eacute;Exchange</span>
    <span>26 August 2026</span>
    <span>Read this before we write any code</span>
  </div>
  <div class="tocbox">
    <ol>
      <li><b>Part 0</b><span>What problem does an exchange actually solve?</span></li>
      <li><b>Part 1</b><span>Orders, the book, and the vocabulary</span></li>
      <li><b>Part 2</b><span>Matching &mdash; the fifteen lines at the centre</span></li>
      <li><b>Part 3</b><span>The fiddly rules, and why each one exists</span></li>
      <li><b>Part 4</b><span>Why this is a hard engineering problem</span></li>
      <li><b>Part 5</b><span>The mathematics, explained slowly</span></li>
      <li><b>Part 6</b><span>Making the Pok&eacute;mon layer do real work</span></li>
      <li><b>Part 7</b><span>What we build, in what order, and why</span></li>
      <li><b>Glossary</b><span>Every term, one line each</span></li>
    </ol>
  </div>
</section>
""")

# ----------------------------------------------------------------------- part 0
add("""
<h2 class="part"><span class="num">Part 0</span>What problem does an exchange
actually solve?</h2>
<p class="standfirst">Before any code, the human problem. Everything the engine
does is a mechanical answer to a difficulty that existed long before computers.</p>

<p>Suppose you own a Charizard and you want money instead. You have a problem with
three separate parts, and they are worth naming because the exchange solves each
one with a different piece of machinery.</p>

<p><b>First, search.</b> Somewhere out there is a person who wants a Charizard more
than they want their money. You do not know who they are. Finding them costs you
time, and the longer it takes, the more likely you accept a worse deal out of sheer
impatience.</p>

<p><b>Second, price.</b> Even once you find them, neither of you knows what a
Charizard is <em>worth</em>. You will haggle. The outcome will depend on who is more
desperate and who is better at haggling &mdash; not on any underlying truth. Two
identical cards will sell for different amounts on the same afternoon.</p>

<p><b>Third, trust.</b> You must hand over the card before, after, or at the same
moment as they hand over the money. Somebody has to go first, and going first is
dangerous.</p>

<h3>The exchange&rsquo;s answer</h3>
<p>An exchange is a single public place where everyone who wants to buy or sell
posts their intentions in a standard format. That one move dissolves all three
problems at once. Search collapses, because everyone is already in the room. Price
becomes a public fact rather than a private negotiation, because you can see what
everyone else is willing to pay. And trust stops mattering, because you are not
dealing with a stranger &mdash; you are dealing with the exchange, which guarantees
the swap.</p>

<div class="key"><span class="lab">The single most important idea in this document</span>
<p><b>The exchange never trades.</b> It owns nothing, wants nothing, and has no
opinion about whether Charizard is expensive. It is a referee. Its only job is to
take everybody&rsquo;s stated intentions and pair them up according to rules that
were published in advance, so that nobody can afterwards claim they were cheated.</p>
<p>Every design decision in the rest of this document is a consequence of three
words: <b>fair</b>, <b>fast</b>, and <b>auditable</b>. When you cannot remember why
we did something, it will be one of those three.</p></div>

<h3>Two ways to run the room</h3>
<p>You could collect everybody&rsquo;s intentions all morning, then at noon work out
the single price that lets the most trades happen, and execute everything at once.
That is a <b>call auction</b>, and real exchanges genuinely do this at the open and
close of each day. It is fair and it is efficient, but you can only trade at noon.</p>

<p>Or you could match people the instant a compatible pair exists, continuously, all
day. That is a <b>continuous double auction</b> &mdash; &ldquo;double&rdquo; because
both buyers and sellers post prices, rather than one side setting prices and the
other taking them. This is what essentially every modern market does, and it is what
we are building. The cost of continuous trading is that <em>time now matters</em>:
if two people want the same thing, the one who asked first should get it. Hold onto
that, because it is about to become the central rule of the whole system.</p>
""")

# ----------------------------------------------------------------------- part 1
add("""
<h2 class="part"><span class="num">Part 1</span>Orders, the book, and the
vocabulary</h2>
<p class="standfirst">Six words &mdash; bid, ask, spread, mid, depth, liquidity
&mdash; carry most of the meaning in this field. Here they are, with pictures.</p>

<h3>An order is a sentence with four parts</h3>
<p>Everything a participant can say to the exchange reduces to one sentence:
<em>&ldquo;I want to <b>buy or sell</b>, this <b>many</b>, at this <b>price</b> or
better, under these <b>conditions</b>.&rdquo;</em> That is it. There is no richer
language. The two important kinds:</p>

<p><b>A limit order</b> names a price and waits. &ldquo;Buy 100 at 99&rdquo; means
<em>buy 100 for me, but never pay more than 99</em>. If nobody will sell that
cheaply right now, the order does not fail and it does not compromise &mdash; it
<b>rests</b> in the book and waits, possibly forever. A limit order is patience
written down.</p>

<p><b>A market order</b> names no price. &ldquo;Buy 100, now&rdquo; means <em>get me
100, I accept whatever it costs</em>. It never rests, because there is nothing to
wait for; it consumes whatever is available and stops. A market order is urgency
written down.</p>

<div class="aside"><span class="lab">Why the asymmetry matters</span>
<p>A limit order can be left unfilled but never gives a bad price. A market order is
always filled but can give a terrible price. You are choosing which risk you would
rather carry &mdash; <b>price risk or execution risk</b> &mdash; and you cannot
avoid both. Essentially all of trading strategy is that one trade-off, applied over
and over.</p></div>

<h3>The book is just the pile of unmatched patience</h3>
<p>At any instant, some limit orders have not found a counterparty. Sort them by
price and you have the <b>limit order book</b>: buyers on one side, sellers on the
other, and a gap in the middle where nobody agrees.</p>
""", ladder_svg(), """
<p>Read Figure 1 once more, slowly, because five of our six words are visible in it:</p>

<table>
<tr><th style="width:24mm">Word</th><th style="width:44mm">Means</th><th>In Figure 1</th></tr>
<tr><td><b>Bid</b></td><td>a price someone will buy at</td>
    <td>the blue side. Buyers naturally want to pay <em>less</em>, so bids sit below.</td></tr>
<tr><td><b>Ask</b> (or offer)</td><td>a price someone will sell at</td>
    <td>the red side. Sellers want to receive <em>more</em>, so asks sit above.</td></tr>
<tr><td><b>Best bid / best ask</b></td><td>the highest bid, the lowest ask</td>
    <td>99 and 101 &mdash; the two prices closest to the gap. Together they are
        called the <em>top of book</em>, and they are the only two prices most
        people ever look at.</td></tr>
<tr><td><b>Spread</b></td><td>best ask &minus; best bid</td>
    <td>101 &minus; 99 = <b>2 ticks</b>. The width of the disagreement.</td></tr>
<tr><td><b>Mid</b></td><td>the average of the two</td>
    <td>100. Not a price anybody is offering &mdash; a convention, our best single
        guess at &ldquo;the&rdquo; price.</td></tr>
<tr><td><b>Depth</b></td><td>how much size is resting</td>
    <td>the bar lengths. 50 at the best ask, but 400 three ticks up.</td></tr>
</table>

<h3>Why the spread cannot be zero</h3>
<p>A natural question: if buyers want to buy and sellers want to sell, why is there
a gap at all? Because the best bid being <em>below</em> the best ask is not an
accident &mdash; it is a mathematical necessity. If the best bid ever rose to meet
the best ask, those two orders would immediately trade and vanish, re&euml;stablishing
the gap. A book with a bid at or above the ask is called <b>crossed</b>, and it is a
state that can only exist for the instant before the engine resolves it.</p>

<div class="key"><span class="lab">Our first hard invariant</span>
<p><b>best bid &lt; best ask, always, at every moment an outside observer can look.</b>
This is one of the properties our fuzz tests will assert after every single message.
If we ever violate it, the engine is broken &mdash; not slow, not suboptimal,
<em>broken</em>, because it means we failed to execute a trade that both parties had
already agreed to.</p></div>

<h3>The same book, seen as a cost curve</h3>
<p>The ladder tells you what is available at each price. Often the more useful
question is cumulative: <em>if I keep buying until I have spent my way up to price
P, how much will I have got?</em> That is the depth curve.</p>
""", depth_svg(), """
<h3>Liquidity, defined properly</h3>
<p><b>Liquidity</b> is the most abused word in finance. Used precisely, it means:
<em>the ability to trade a meaningful size, quickly, without moving the price much.</em>
It has three independent dimensions, and Figure 2 shows two of them directly:</p>
<ul>
<li><b>Tightness</b> &mdash; how narrow the spread is. The cost of a tiny trade.</li>
<li><b>Depth</b> &mdash; how much size sits at each level. The cost of a large trade.</li>
<li><b>Resiliency</b> &mdash; how fast the book refills after being eaten. Not
visible in a snapshot; you need to watch the book over time. This one only becomes
measurable once we have agents, in sub-project B.</li>
</ul>
<p>A market can be tight but shallow &mdash; a one-tick spread with only 5 units
behind it, which is wonderful if you want 5 and useless if you want 500. This is
precisely the situation we will engineer deliberately with Legendaries: a tiny
float, a razor-thin book, and a chance to watch what happens to anyone who tries to
trade size in it.</p>

<div class="aside"><span class="lab">The bargain at the heart of every market</span>
<p>Someone posting a limit order is <b>providing</b> liquidity: they are standing
there offering everyone else the option to trade instantly. Someone sending a market
order is <b>taking</b> it. The provider gets paid for that service &mdash; in
expectation, roughly half the spread &mdash; and in exchange accepts the risk of
being on the wrong side when the price moves. The taker pays the spread and buys
certainty.</p>
<p>The spread is therefore not a fee, and it is not friction. <b>It is the price of
immediacy</b>, set by competition between the people willing to sell it.</p></div>
""")

# ----------------------------------------------------------------------- part 2
add("""
<h2 class="part"><span class="num">Part 2</span>Matching &mdash; the fifteen lines
at the centre</h2>
<p class="standfirst">The part everyone assumes is complicated. It is not. What is
hard is making it fast, and that is Part 4&rsquo;s problem.</p>

<h3>The fairness rule: price, then time</h3>
<p>When an incoming order could trade against several resting orders, which one wins?
The rule is <b>price-time priority</b>, and it has exactly two clauses applied in
order:</p>
<ol>
<li><b>Better price first.</b> If you are selling, whoever bids most gets served
first. This is not charity, it is efficiency: it guarantees trades happen at the
best available price, which is the entire point of gathering everyone in one room.</li>
<li><b>Then earlier arrival.</b> Among orders at <em>the same</em> price, the one
that has been waiting longest goes first. Strict first-in-first-out.</li>
</ol>
<p>Clause two is the interesting one, because it is a pure fairness rule with no
efficiency justification &mdash; the exchange gets an identical outcome either way.
It exists because a queue that anyone can jump is a queue nobody will join, and an
exchange with no resting orders is not an exchange. <b>Time priority is the payment
for patience</b>, and it is what makes providing liquidity worth doing at all.</p>

<h3>Watching an order eat the book</h3>
<p>Now the mechanism. An incoming order that can trade immediately is called
<b>aggressive</b> or a <b>taker</b>; the resting orders it trades against are
<b>passive</b>, or <b>makers</b>. The taker walks the opposite side of the book,
cheapest first, printing a trade at each stop, until it is satisfied or the book
runs dry.</p>
""", sweep_svg(), """
<h3>The single most-failed detail: whose price wins?</h3>
<p>In Figure 3 the buyer bought at 101 and then at 102. Suppose instead of a market
order they had sent a <em>limit</em> order to buy 180 at 105. What price should the
first 50 trade at &mdash; 101, or 105?</p>
<p><b>101.</b> Always the resting order&rsquo;s price, never the incoming
order&rsquo;s. The reasoning is clean: the resting seller published terms
(&ldquo;I will sell at 101&rdquo;) and has been standing behind them, in public,
waiting. The arriving buyer read those terms and chose to accept them. You do not
get to charge somebody more than they advertised because you discovered they would
have paid it.</p>
<p>The buyer therefore receives four ticks of <b>price improvement</b> &mdash; they
were willing to pay 105 and paid 101. This is a real and routine occurrence, it is
the direct reward for posting a wide limit rather than a market order, and getting it
backwards is the most common bug in a first matching engine.</p>

<h3>Partial fills and residuals</h3>
<p>In panel ③ the incoming buyer needed 130 but the level held 150. The resting order
is filled for 130 and <b>remains in the book with 20 left</b>, keeping its original
place in the queue &mdash; it did nothing wrong, so it loses nothing. Correspondingly
an order can be filled in twenty pieces at five different prices over an hour. So
every order carries two quantities forever: what it originally asked for, and how
much is still outstanding. Confusing those two is bug number two.</p>

<h3>The whole algorithm</h3>
<p>Here it is in full. Not a sketch &mdash; this is genuinely the complete
description of what a matching engine does.</p>
<pre><span class="c">// an order arrives to BUY `qty` at `limit`</span>
while  remaining &gt; 0
   and  book has an ask
   and  best_ask &lt;= limit:                 <span class="c">// is there a price we can accept?</span>

     resting = front of level[best_ask]      <span class="c">// front == oldest == time priority</span>
     fill    = min(remaining, resting.remaining)

     print TRADE at best_ask for fill        <span class="c">// the MAKER's price</span>
     remaining         -= fill
     resting.remaining -= fill

     if resting.remaining == 0: remove it from the queue
     if level is now empty:     drop the level

if remaining &gt; 0:                            <span class="c">// we could not fill it all</span>
     LIMIT  -&gt; insert at back of level[limit] <span class="c">// join the queue and wait</span>
     MARKET -&gt; cancel the remainder           <span class="c">// nothing left to hit</span>
</pre>

<div class="key"><span class="lab">Where the difficulty actually lives</span>
<p>Read that loop and count the operations it needs: <b>find the best price</b>,
<b>get the front of a queue</b>, <b>remove from a queue</b>, <b>insert at the back of
a queue</b>. Four operations. Plus one more that does not even appear above:
<b>find an arbitrary order by its ID</b>, which is what cancelling needs.</p>
<p><b>Those five operations are the entire engineering project.</b> The loop above
will be written once, in an afternoon, and never meaningfully change again. Parts
v0 through v4 of our build are five different attempts to make those five operations
faster, and nothing else.</p></div>

<h3>One more requirement: it must be repeatable</h3>
<p>An exchange settles real obligations, so &ldquo;why did my order not fill?&rdquo;
must have an exact answer, reproducible months later. That gives us the strongest
constraint in the whole design: <b>determinism</b>. The same sequence of input
messages must produce a byte-for-byte identical sequence of output events, on any
machine, forever.</p>
<p>Which forbids more than you would expect. No reading the wall clock inside the
engine &mdash; time priority is established by an integer counter that ticks once per
accepted message, not by a timestamp. No random numbers. No iterating a hash map
whose order depends on where the allocator happened to put things. No relying on
which thread got there first. Determinism is not a testing convenience we are being
fastidious about; it is the property that makes the two most valuable things in the
project possible at all:</p>
<ul>
<li><b>Differential testing.</b> Write the engine twice &mdash; once carefully in
C++, once simply in Python &mdash; feed both the same million random messages, and
demand identical output. Two independent implementations agreeing is a far stronger
correctness claim than any hand-written test suite.</li>
<li><b>An honest benchmark.</b> To claim v3 is faster than v0 we must prove they did
the <em>same work</em>. Determinism lets us assert they produced identical trades, so
the only thing that differed was the speed.</li>
</ul>
""")

# ----------------------------------------------------------------------- part 3
add("""
<h2 class="part"><span class="num">Part 3</span>The fiddly rules, and why each one
exists</h2>
<p class="standfirst">Every rule below was invented in response to somebody getting
hurt. Knowing the injury is how you remember the rule.</p>

<h3>The order types</h3>
<table>
<tr><th style="width:30mm">Type</th><th style="width:58mm">Behaviour</th>
    <th>The problem it solves</th></tr>
<tr><td><b>Limit</b></td>
    <td>Trade what you can at your price or better; rest the remainder.</td>
    <td>The default. Never a bad price, but possibly no fill.</td></tr>
<tr><td><b>Market</b></td>
    <td>Trade until filled, at any price; cancel any remainder.</td>
    <td>Certainty of getting done. Dangerous in a thin book &mdash; see Part 5 on
        slippage.</td></tr>
<tr><td><b>IOC</b><br><span class="mono" style="font-size:8.6pt">immediate-or-cancel</span></td>
    <td>Trade what you can right now at your price; cancel the rest instantly rather
        than resting.</td>
    <td>&ldquo;Take what&rsquo;s there, but do not leave me exposed.&rdquo; A resting
        order broadcasts your intentions to everyone. IOC lets you take liquidity
        without ever showing your hand.</td></tr>
<tr><td><b>FOK</b><br><span class="mono" style="font-size:8.6pt">fill-or-kill</span></td>
    <td>Fill the <em>entire</em> quantity immediately or do nothing at all. All or
        nothing.</td>
    <td>When a partial fill is worse than no fill &mdash; e.g. one leg of a
        multi-instrument trade. Buying half a hedge leaves you more exposed than
        buying none of it.</td></tr>
<tr><td><b>Post-only</b></td>
    <td>Must rest. If it would trade immediately, reject it instead.</td>
    <td>Guarantees you are the maker, never the taker. Matters because exchanges
        usually charge takers and pay makers a small <em>rebate</em>; a post-only
        order is a promise you will not accidentally pay the fee.</td></tr>
</table>

<h3>Cancel and modify: the interview question</h3>
<p>Cancel is simple to state (&ldquo;remove order 8,271,993&rdquo;) and, as Part 4
will show, the single most performance-critical operation in the entire system.
Modify is where the interesting question lives. Say you have an order resting in the
queue and you want to change it. Do you keep your place in line?</p>

<table>
<tr><th style="width:56mm">You want to&hellip;</th><th style="width:34mm">Priority</th>
    <th>Because</th></tr>
<tr><td><b>Reduce</b> the quantity</td><td><b>Keep it</b> &mdash; stay where you are</td>
    <td>You are asking for <em>less</em>. Nobody behind you is harmed; in fact
        everyone behind you moves closer to the front. There is no one to compensate,
        so there is nothing to charge you.</td></tr>
<tr><td><b>Increase</b> the quantity</td><td><b>Lose it</b> &mdash; go to the back</td>
    <td>The extra quantity never waited. If you kept your slot you would be inserting
        brand-new size ahead of people who have been queuing &mdash; a strictly better
        deal than they got, for free. That is queue-jumping.</td></tr>
<tr><td><b>Change</b> the price</td><td><b>Lose it</b> &mdash; back of the new level</td>
    <td>Priority is a property <em>of a price level</em>, not of you. You have
        never queued at the new price for even a microsecond.</td></tr>
</table>

<div class="aside"><span class="lab">How to answer this in an interview</span>
<p>Do not recite the table. State the principle and derive the table from it in real
time: <b>time priority is compensation for exposure risk, so you keep priority
exactly as long as you have not increased the risk anyone else is taking on your
behalf.</b> Reducing quantity lowers your exposure; everything else raises it.</p>
<p>Then add the implementation note, because it is what they are really probing:
&ldquo;which means a reduce is an in-place decrement of one field, but an increase or
a reprice is a cancel followed by a fresh insert &mdash; so I implement modify in
terms of the two primitives I already have, rather than as a third code path that can
disagree with them.&rdquo;</p></div>

<h3>Self-trade prevention</h3>
<p>Nothing stops your own buy order from matching your own sell order. The trade is
economically meaningless &mdash; you pay yourself &mdash; but it is not harmless. It
prints on the public tape, so it manufactures the appearance of activity that never
happened. Done deliberately and repeatedly, that is <b>wash trading</b>: illegal
market manipulation, because it fakes the volume signal other participants rely on.
Exchanges therefore refuse the match outright. Which of the two orders gets cancelled
is a published policy choice; we will cancel the incoming one, as it is the simpler
rule to reason about and to test.</p>

<h3>Rejections are data, not exceptions</h3>
<p>Bad input &mdash; an unknown instrument, a zero quantity, a cancel for an order
that already filled &mdash; produces a <b>Rejected</b> event carrying a reason code,
and the book is left completely untouched. The engine never throws and never logs.</p>
<p>Two reasons, and they are worth understanding because this is the kind of decision
that reads as pedantic until it saves you. First, a function that can throw halfway
through a match can leave the book half-updated, and a half-updated book is not
recoverable &mdash; you have lost the audit trail, which was the whole point.
Second, an error path that is <em>data</em> gets exercised by the same tests as
everything else, whereas an error path that is an exception is the code nobody ever
tests. Malformed input is not an exceptional circumstance in a system open to the
public. It is Tuesday.</p>
""")

# ----------------------------------------------------------------------- part 4
add("""
<h2 class="part"><span class="num">Part 4</span>Why this is a hard engineering
problem</h2>
<p class="standfirst">The algorithm is fifteen lines. The performance work is weeks.
This part explains that gap, because the gap is the reason the project is worth
building.</p>

<h3>The traffic is not what you would guess</h3>
<p>Ask someone to guess the message mix at a real exchange and they will say mostly
orders, some trades, a few cancels. It is almost exactly inverted. A representative
mix looks like this:</p>

<table>
<tr><th>Message</th><th class="num" style="width:26mm">Share</th><th>Consequence for us</th></tr>
<tr><td><b>Cancel</b></td><td class="num">~90%</td>
    <td>Automated strategies re-quote constantly &mdash; post, reconsider, pull, repost,
        thousands of times a second. <b>This is the operation to optimise.</b></td></tr>
<tr><td><b>New order</b></td><td class="num">~9%</td>
    <td>Needs a fast insert at the back of a queue.</td></tr>
<tr><td><b>Actual trade</b></td><td class="num">~1%</td>
    <td>The rarest event in the system, and the one everyone thinks about first.</td></tr>
</table>

<p>Sit with that for a second, because it inverts the natural design instinct. If you
set out to build a matching engine you would optimise <em>matching</em>. But roughly
nine in ten messages never touch the matching loop at all &mdash; they are somebody
saying &ldquo;forget order 8,271,993&rdquo;. So the hot path is not the loop from
Part 2. The hot path is <b>find one order among millions, by ID, and unlink
it &mdash; right now.</b> Everything below follows from that sentence.</p>

<div class="flag"><span class="lab">Caveat on these figures</span>
<p>The 90/9/1 split is the widely-reported shape of message traffic on major
electronic venues, quoted here as an order of magnitude rather than a precise
statistic &mdash; exact ratios vary enormously by venue, instrument and year. We
should cite a specific published figure in the final README rather than this
approximation, and we will be able to measure our <em>own</em> mix exactly, which is
what the benchmark actually needs.</p></div>

<h3>The reason fast code is not about instruction count</h3>
<p>Here is the fact that reorganises how you think about performance. A modern CPU
core executes several billion instructions per second. It can also be made to sit
completely idle for a hundred nanoseconds &mdash; several hundred instructions&rsquo;
worth of time &mdash; waiting for a single number to arrive from memory.</p>
""", memory_svg(), """
<p>The CPU never fetches one value. It fetches a 64-byte <b>cache line</b> and hopes
you wanted the neighbours too. So the question that determines your speed is not
<em>how many operations do I perform</em>, it is <b>how often is the thing I need
next already sitting next to the thing I just touched</b>. Which makes it a question
about memory <em>layout</em>, not about algorithms.</p>

<p>This is why <code>std::map</code> &mdash; a perfectly good, O(log n), textbook-correct
data structure &mdash; is the wrong tool. Each node was allocated separately,
whenever it happened to be created, so the nodes are scattered across memory. Walking
from one price level to the next is a <b>pointer chase</b>: read a pointer, wait for
memory, read the next pointer, wait again. The asymptotic complexity is fine. The
constant factor is a disaster.</p>
""", pointer_svg(), """
<p>And the fix is available to us because of a fact about prices specifically:
<b>prices are discrete</b>. They move in ticks, and a tick count is an integer. So
&ldquo;the book&rdquo; can simply be an array with one slot per possible price, and
finding a price level becomes address arithmetic &mdash; no search, no pointers, no
tree. This is the observation the entire optimisation programme rests on, and it is
also the reason Part 1&rsquo;s insistence that prices are integers and never floats
was not fussiness. An array cannot be indexed by 100.35.</p>

<h3>The five versions, and what each one buys</h3>
<table>
<tr><th style="width:13mm">Ver</th><th style="width:52mm">Change</th>
    <th>The specific problem it attacks</th></tr>
<tr><td class="mono"><b>v0</b></td><td><code>std::map&lt;Price, deque&lt;Order&gt;&gt;</code></td>
    <td>None &mdash; it is the honest baseline, and we keep it forever. Without a
        slow version there is no story, only an assertion.</td></tr>
<tr><td class="mono"><b>v1</b></td><td>Array indexed by tick</td>
    <td>Pointer chasing and tree rebalancing. Finding a level becomes arithmetic.
        Best bid/ask tracked with a moving pointer or a bitmask so we never scan
        for it.</td></tr>
<tr><td class="mono"><b>v2</b></td><td>Intrusive linked list + object pool</td>
    <td><code>malloc</code> on the hot path. Every allocation is a function call
        that may take a lock and may wander off to the OS. We pre-allocate all the
        order slots once and hand them out from a free list, so steady-state
        operation allocates nothing at all. &ldquo;Intrusive&rdquo; means the
        list pointers live inside the order object, so an order and its links
        arrive in the same cache line.</td></tr>
<tr><td class="mono"><b>v3</b></td><td>Open-addressing hash: ID &rarr; node*</td>
    <td><b>The big one.</b> Cancel, our ~90% case, goes from &ldquo;search the
        book&rdquo; to one hash lookup plus two pointer writes to unlink. Open
        addressing rather than <code>std::unordered_map</code> because the latter is
        a linked list per bucket &mdash; more pointer chasing, exactly what we are
        trying to escape.</td></tr>
<tr><td class="mono"><b>v4</b></td><td>Lock-free SPSC ring buffer</td>
    <td>Stretch goal. Separates the thread reading the network from the thread doing
        the matching, so neither waits on the other. Requires cache-line-aligning the
        two shared indices, or the two threads fight over one cache line and you get
        <b>false sharing</b> &mdash; a slowdown with no logical cause, and a genuinely
        good thing to have debugged once.</td></tr>
</table>

<h3>Measuring it without fooling ourselves</h3>
<p>A benchmark is a claim, and most benchmark claims are wrong. Ours has to survive
someone hostile reading it, so:</p>
<ul>
<li><b>Report the distribution, never the average.</b> An average latency hides
exactly the thing that matters. If one order in a thousand takes fifty times as long,
your mean barely moves &mdash; and in a real market that one order is where you get
picked off. So we quote <b>p50, p99, p99.9 and max</b>, and we show the histogram.</li>
<li><b>Identical input to every version.</b> One recorded message file, replayed into
each version, with the outputs asserted byte-identical. Determinism is what makes
this possible.</li>
<li><b>Measure outside the engine.</b> Timing instrumentation inside the matching
loop would change what it is measuring.</li>
<li><b>Discard the warm-up.</b> The first few thousand messages measure a cold cache
and an un-JITed branch predictor, not steady-state behaviour.</li>
</ul>
""", lat_hist_svg(), """
<div class="flag"><span class="lab">A real constraint we have to plan around</span>
<p>You are on an Apple-silicon Mac, and two of the tools the spec assumes do not
exist there. <code>rdtsc</code> is an x86 instruction; the ARM equivalent is reading
the <code>cntvct_el0</code> system register, which ticks at a different and lower
frequency. And <code>perf stat</code> &mdash; which is how you would get cache-miss
and branch-misprediction counts &mdash; is Linux-only. macOS offers
<code>xctrace</code>, which is considerably less direct.</p>
<p>This does not block sub-project A at all, and I have kept the timing abstraction
outside the engine so it cannot contaminate the design. But before sub-project C we
need to choose: a cheap Linux cloud box for the benchmark runs, or a local VM, or
accept weaker profiling evidence on macOS. My recommendation is the Linux box &mdash;
<code>perf stat</code> output showing the cache-miss rate falling between v0 and v1
is one of the most persuasive artefacts this project can produce, and it is worth a
few dollars.</p></div>
""")

# ----------------------------------------------------------------------- part 5
add("""
<h2 class="part"><span class="num">Part 5</span>The mathematics, explained slowly</h2>
<p class="standfirst">Ten pieces of maths, in dependency order. Each one is stated,
then taken apart term by term, then justified. None of it requires anything beyond
first-year calculus and probability.</p>

<h3>5.1 &nbsp;Returns &mdash; and why we take logarithms</h3>
<p>Prices themselves are nearly useless for analysis: knowing Charizard is at 4,300
tells you nothing without knowing where it was. What carries information is
<b>change</b>, expressed as a fraction so that instruments of different price can be
compared. That is a <b>return</b>, and we almost always use the log form:</p>
""", '<div class="eq">r<sub>t</sub> &nbsp;=&nbsp; ln ' + frac('P<sub>t</sub>', 'P<sub>t&minus;1</sub>') +
 '<span class="where"><b>P<sub>t</sub></b> the price now &middot; <b>P<sub>t&minus;1</sub></b> '
 'the price one step ago &middot; <b>ln</b> the natural logarithm</span></div>', """
<p>Three reasons for the logarithm, and the first is the one that matters:</p>
<ul>
<li><b>Log returns add up; simple returns do not.</b> Go up 10% then down 10% and you
are at 99, not 100 &mdash; so simple returns cannot be summed over time. Log returns
can: the return over a week is exactly the sum of the daily log returns. This makes
every subsequent formula cleaner.</li>
<li><b>They are symmetric.</b> A halving and a doubling are &minus;0.693 and +0.693.
With simple returns they are &minus;50% and +100%, which makes an unbiased process
look biased.</li>
<li><b>They approximate percentages for small moves.</b> ln(1.01) = 0.00995, so for
the small moves that dominate tick data you can read a log return as a percentage
and be right to three decimal places.</li>
</ul>

<h3>5.2 &nbsp;Volatility &mdash; the size of the wiggle</h3>
<p><b>Volatility</b> is simply the standard deviation of returns. It measures how far
the price typically moves per unit of time, ignoring direction entirely.</p>
""", '<div class="eq">&sigma;<sup>2</sup> &nbsp;=&nbsp; ' + frac('1', 'n &minus; 1')
 + ' &Sigma;<sub>i</sub> (r<sub>i</sub> &minus; r&#772;)<sup>2</sup>'
 '<span class="where">This is the <b>variance</b>; volatility &sigma; is its square root. '
 '<b>r&#772;</b> the mean return &middot; <b>n</b> the number of observations &middot; the '
 '<b>n&minus;1</b> rather than <b>n</b> is Bessel&rsquo;s correction, because we estimated the '
 'mean from the same data</span></div>', """
<p>Volatility is the single most important number in this project, because it appears
in nearly every formula that follows. It sets how wide a market maker should quote
(5.8), how large the price impact of a trade is (5.5), and whether a divergence
between two instruments is meaningful or noise (5.10). It is also <em>not
constant</em> &mdash; that fact is a whole stylized fact of its own, in 5.6.</p>

<h3>5.3 &nbsp;Mid price, and the better version nobody teaches first</h3>
<p>The obvious estimate of &ldquo;the&rdquo; price is the midpoint:</p>
""", '<div class="eq">mid &nbsp;=&nbsp; ' + frac('P<sub>bid</sub> + P<sub>ask</sub>', '2') + '</div>', """
<p>It is fine, and it is also throwing away information. Look again at Figure 1: 200
resting on the bid at 99, but only 50 on the ask at 101. Far more people want to buy
than to sell. The mid says 100, but 100 is clearly not where this book is heading.
The <b>microprice</b> (or weighted mid) captures that:</p>
""", '<div class="eq">micro &nbsp;=&nbsp; ' +
 frac('P<sub>bid</sub>&middot;Q<sub>ask</sub> &nbsp;+&nbsp; P<sub>ask</sub>&middot;Q<sub>bid</sub>',
      'Q<sub>bid</sub> + Q<sub>ask</sub>') +
 '<span class="where">Note the <b>crossed</b> weighting: each price is weighted by the '
 'quantity on the <em>opposite</em> side.</span></div>', """
<p>The crossing looks like a typo and is the entire idea. A large bid quantity is
evidence of buying pressure, so it should pull the fair value <em>up</em>, towards
the ask. Working Figure 1 through: (99&times;50 + 101&times;200) / 250 = 100.6 &mdash;
noticeably above the mid of 100, which is exactly right, because that book is
lopsided towards buyers.</p>
<div class="aside"><span class="lab">Why this matters to us</span>
<p>Any agent that quotes around the plain mid in a lopsided book is systematically
quoting too low on one side, and will be picked off there. The microprice is a
one-line change that makes our market maker meaningfully less naive, and it is the
kind of detail that shows you have thought about the book rather than just read about
it.</p></div>

<h3>5.4 &nbsp;What a trade actually costs: VWAP and slippage</h3>
<p>Return to Figure 3 &mdash; the market order to buy 180. It did not trade at one
price; it traded 50 at 101 and 130 at 102. The single number describing what it paid
is the <b>volume-weighted average price</b>:</p>
""", '<div class="eq">VWAP &nbsp;=&nbsp; ' + frac('&Sigma;<sub>i</sub> p<sub>i</sub> q<sub>i</sub>', '&Sigma;<sub>i</sub> q<sub>i</sub>')
 + ' &nbsp;=&nbsp; ' + frac('50&times;101 &nbsp;+&nbsp; 130&times;102', '180') + ' &nbsp;=&nbsp; '
 + frac('18,310', '180') + ' &nbsp;=&nbsp; <b>101.72</b></div>', """
<p><b>Slippage</b> is the gap between the price you hoped for and the VWAP you got.
Measured against the mid of 100 it is 1.72 ticks; measured against the best ask of
101 it is 0.72. Both are quoted in practice, so always say which. That 0.72 is the
cost of size &mdash; the first 50 got the good price and the next 130 did not.</p>
<div class="key"><span class="lab">This is the Legendary experiment in one number</span>
<p>Slippage is determined entirely by the shape of the depth curve. Give a Legendary
a float of a few hundred units and its book will be three levels deep, so an order
of any consequence sweeps through all of them and the slippage is enormous. Same
engine, same order, wildly different cost &mdash; because of nothing but market
structure. This is the microstructure lesson the theme lets us make visible.</p></div>

<h3>5.5 &nbsp;Market impact: the square-root law</h3>
<p>Slippage measures what one order paid. <b>Impact</b> asks the broader question:
how far does the price <em>move</em> when someone trades size Q? The empirical answer
is remarkably consistent across every market anyone has measured, and it is not
linear:</p>
""", '<div class="eq">&Delta;P &nbsp;&asymp;&nbsp; Y &middot; &sigma; &middot; '
 '&radic;<span class="rad">Q&thinsp;/&thinsp;V</span>'
 '<span class="where"><b>Q</b> your order size &middot; <b>V</b> the instrument&rsquo;s total '
 'volume over the same period &middot; <b>&sigma;</b> its volatility &middot; <b>Y</b> a '
 'dimensionless constant, empirically around 0.5&ndash;1</span></div>', """
<p>Read the formula in words: <em>impact is volatility, scaled by the square root of
the fraction of the market you are.</em> The square root is the surprising part and
the useful part.</p>
""", impact_svg(), """
<p><b>Why a square root and not a straight line?</b> There is no single agreed
derivation, which is itself worth knowing &mdash; this is an empirical law that
theory has chased rather than predicted. The most intuitive account: the book is not
a fixed queue you consume, it is a population of latent traders who react. As you
buy, some sellers who were not previously interested are drawn in by the higher
price. Your own trading partly refills the book ahead of you, so the marginal cost of
each extra unit falls. Concavity follows.</p>
<p>The practical consequence is enormous and is why execution is a profession:
splitting one large order into many small ones spread over time genuinely costs less
than sending it at once, because you keep re-entering at the cheap end of the curve.
Whether our simulated market reproduces a square root &mdash; rather than a line, or
some other exponent &mdash; is one of the sharpest tests of whether the agent
population is behaving like real people.</p>

<h3>5.6 &nbsp;Stylized facts: the fingerprints of a real market</h3>
<p>Financial return series, across every asset class and century anyone has checked,
share a set of statistical properties. They are called <b>stylized facts</b>. They
matter to us for a specific reason: <b>we are not going to programme them in.</b>
They should <em>emerge</em> from agents interacting. If they do, our simulation has
captured something true about how markets work. If they do not, we have built an
expensive random number generator.</p>

<h4>Fact 1 &mdash; Returns are fat-tailed</h4>
<p>Extreme moves happen far more often than a bell curve allows. The standard measure
is <b>kurtosis</b>, the normalised fourth moment:</p>
""", '<div class="eq">&kappa; &nbsp;=&nbsp; ' +
 frac('E[(r &minus; &mu;)<sup>4</sup>]', '&sigma;<sup>4</sup>') +
 '<span class="where">A Normal distribution has &kappa; = 3 exactly. Anything above 3 has '
 'fatter tails; financial returns typically land between 4 and 20.</span></div>', """
<p>The fourth power is what makes this a tail measure: a move of 5&sigma; contributes
625 times as much as a move of 1&sigma;, so &kappa; is dominated almost entirely by
the rare extremes and barely notices the ordinary days.</p>
""", ret_dist_svg(), """
<p>The chart above is a genuinely simulated series, and its kurtosis is
""" + f"<b>{KURT:.2f}</b>" + """ against 3.00 for a Normal. Notice what that means on a
log scale: out past &plusmn;5&sigma; the dashed Normal curve has effectively hit
zero &mdash; it is claiming such days are impossible &mdash; while the bars are
still clearly there. Assuming normality is not a small error at the tails. It is a
categorical one, and it is the mistake behind a good number of famous blow-ups.</p>

<h4>Fact 2 &mdash; Direction is unpredictable, but size is not</h4>
<p>The tool for both halves of that sentence is the <b>autocorrelation function</b>
&mdash; how much a series resembles itself, shifted by k steps:</p>
""", '<div class="eq">&rho;(k) &nbsp;=&nbsp; ' +
 frac('&Sigma;<sub>t</sub> (r<sub>t</sub> &minus; r&#772;)(r<sub>t+k</sub> &minus; r&#772;)',
      '&Sigma;<sub>t</sub> (r<sub>t</sub> &minus; r&#772;)<sup>2</sup>') +
 '<span class="where">&rho;(k) = 1 means perfectly self-similar at lag k; 0 means no linear '
 'relationship at all.</span></div>', """
<p>Apply it twice, and you get one of the most elegant results in the field.</p>
""", acf_svg(), """
<ul>
<li><b>&rho;(k) of returns &asymp; 0</b> for every k. Whether the price just went up
tells you nothing about whether it goes up next. This is not an accident, it is an
equilibrium: any reliable pattern would be traded away by exactly the arbitrageurs we
are going to build. <b>Market efficiency is a consequence of competition, not an
assumption.</b></li>
<li><b>&rho;(k) of |returns| &gt; 0</b>, and it decays slowly over many lags. The
<em>magnitude</em> is highly predictable. Big moves cluster with big moves, quiet with
quiet. This is <b>volatility clustering</b>, and its intuition is simple: the arrival
of news is itself bursty, and it takes the market a while to digest each burst.</li>
</ul>
<p>So you cannot forecast direction but you <em>can</em> forecast turbulence &mdash;
which is precisely why the market maker in 5.8 has &sigma; in its formula and widens
its quotes when &sigma; rises. That agent is monetising Fact 2.</p>

<h3>5.7 &nbsp;When do orders arrive? Poisson processes</h3>
<p>Our noise traders need arrival times. The standard model for &ldquo;events
happening independently at some average rate&rdquo; is the <b>Poisson process</b>:</p>
""", '<div class="eq">P(N events in time t) &nbsp;=&nbsp; ' +
 frac('(&lambda;t)<sup>k</sup> e<sup>&minus;&lambda;t</sup>', 'k!') +
 '<span class="where"><b>&lambda;</b> the average rate (orders per second) &middot; <b>k</b> '
 'the number of events</span></div>', """
<p>The property we actually use is the companion result: if events are Poisson at rate
&lambda;, the <b>gaps between them are exponentially distributed</b> with mean
1/&lambda;. Which gives a one-line generator &mdash; draw U uniform on (0,1] and the
wait until the next order is:</p>
""", '<div class="eq">&Delta;t &nbsp;=&nbsp; &minus;' + frac('ln U', '&lambda;') + '</div>', """
<p>Why is this the right model? Because of the <b>memoryless</b> property: how long
you have already waited tells you nothing about how much longer you will wait. That
is exactly true when arrivals are a great many independent participants each deciding
on their own, which is what a crowd of noise traders is. It is a poor model for
<em>informed</em> flow, which is bursty and correlated &mdash; and that mismatch is a
feature, since it gives our momentum and arbitrage agents a genuinely different
statistical signature from the background.</p>

<h3>5.8 &nbsp;Avellaneda&ndash;Stoikov: how to quote when you can lose money</h3>
<p>This is the most substantial piece of mathematics in the project, so here is the
problem it solves before the formulas.</p>
<p>You are a market maker. You post a bid and an ask, and you earn the spread each
time both get filled. Simple enough &mdash; so why not quote a one-tick spread and
capture enormous volume? Two reasons, pulling in opposite directions:</p>
<ul>
<li><b>Quote too wide</b> and nobody trades with you. You earn a large spread on
almost no volume.</li>
<li><b>Quote too tight</b> and you trade constantly &mdash; but your fills will not
be balanced. A run of buyers leaves you short a pile of Charizard you never wanted,
and now you are exposed to the price moving against you. That exposure is
<b>inventory risk</b>, and it can dwarf everything you earned in spread.</li>
</ul>
<p>Avellaneda and Stoikov posed this as a stochastic optimal control problem &mdash;
maximise the expected utility of your wealth at the end of the session, given that
the price diffuses randomly and your fill probability declines as you quote further
out &mdash; and solved the resulting equation. Two results come out, and both are
interpretable.</p>

<h4>Result 1: quote around your reservation price, not the market&rsquo;s mid</h4>
""", '<div class="eq">r &nbsp;=&nbsp; s &nbsp;&minus;&nbsp; <span class="term">q &middot; &gamma; '
 '&middot; &sigma;<sup>2</sup> &middot; (T &minus; t)</span>'
 '<span class="where"><b>r</b> your reservation price &middot; <b>s</b> the market mid &middot; '
 '<b>q</b> your inventory (positive = long) &middot; <b>&gamma;</b> your risk aversion &middot; '
 '<b>&sigma;<sup>2</sup></b> the variance rate &middot; <b>(T&minus;t)</b> time remaining in '
 'the session</span></div>', """
<p>The <span class="term">underlined term</span> is the whole idea, and every factor
in it earns its place:</p>
<table>
<tr><th style="width:20mm">Factor</th><th>Read it as</th></tr>
<tr><td class="mono"><b>q</b></td><td>How much I am already holding. Zero inventory,
    zero adjustment &mdash; and the sign flips when you go short, so the correction
    always pushes <em>against</em> your position.</td></tr>
<tr><td class="mono"><b>&gamma;</b></td><td>How much I mind risk. A &gamma; of zero
    means you do not care about inventory at all and quote symmetrically around the
    mid forever.</td></tr>
<tr><td class="mono"><b>&sigma;<sup>2</sup></b></td><td>How dangerous holding is
    <em>right now</em>. In a calm market inventory is nearly free; in a violent one
    it is terrifying. This is where Fact 2 gets monetised.</td></tr>
<tr><td class="mono"><b>(T&minus;t)</b></td><td>How long I am stuck with it. Risk is
    proportional to remaining exposure time, so the adjustment shrinks to zero as
    the session closes &mdash; the model naturally becomes indifferent at the end,
    because there is no longer time to be hurt.</td></tr>
</table>
<p>So: <b>if you are long, your reservation price is below the market mid.</b> You
are willing to sell a little cheaper than you &ldquo;should&rdquo;, and you demand a
little more discount to buy more. You have not become bearish; you have priced in the
cost of the risk you are already carrying.</p>
""", as_svg(), """
<h4>Result 2: how wide to make the spread</h4>
""", '<div class="eq">&delta; &nbsp;=&nbsp; <span class="term">&gamma; &sigma;<sup>2</sup> '
 '(T &minus; t)</span> &nbsp;+&nbsp; <span class="term">' + frac('2', '&gamma;') +
 ' ln <span style="font-size:1.2em">(</span>1 + ' + frac('&gamma;', 'k') +
 '<span style="font-size:1.2em">)</span></span>'
 '<span class="where">then quote &nbsp;bid = r &minus; &delta;/2&nbsp; and &nbsp;ask = r + &delta;/2. '
 '<b>k</b> measures how sharply your fill rate falls off as you quote further from the mid.</span></div>', """
<p>Two terms, doing two different jobs:</p>
<ul>
<li><b>&gamma;&sigma;<sup>2</sup>(T&minus;t)</b> &mdash; the <b>risk premium</b>.
Identical in form to the inventory skew, and for the same reason: this is what you
charge for the danger of holding whatever you are about to be handed. Volatility
rises, your spread widens. This is why real spreads blow out during a crash, and it
is not panic &mdash; it is arithmetic.</li>
<li><b>(2/&gamma;)&middot;ln(1 + &gamma;/k)</b> &mdash; the <b>market-power term</b>.
It contains no inventory and no time; it depends only on how price-sensitive your
counterparties are. It comes from modelling the fill intensity as
&lambda;(&delta;) = A&middot;e<sup>&minus;k&delta;</sup>: quote one unit further out
and your fill rate drops by a constant factor. A large k means customers are picky
and abandon you quickly, so you must quote tight. A small k means they will trade
with you almost regardless, so you can charge more. This term is the monopolist&rsquo;s
markup.</li>
</ul>
<div class="flag"><span class="lab">Be precise about this in interviews</span>
<p>These are the closed-form <em>asymptotic approximations</em> from the 2008 paper,
not exact solutions &mdash; the exact problem is a Hamilton&ndash;Jacobi&ndash;Bellman
PDE, and these expressions come from an expansion of it. Saying &ldquo;this is the
approximate solution to the HJB equation under exponential fill intensity&rdquo;
signals that you read the paper. Saying &ldquo;this is the optimal spread&rdquo;
signals that you read a blog post about it.</p></div>

<h3>5.9 &nbsp;Adverse selection: why market making is hard</h3>
<p>There is a nasty asymmetry hiding in 5.8 that the model handles only indirectly,
and it is the single most important concept for understanding why liquidity provision
is not free money.</p>
<p>Your bid rests at 99. Who hits it? Disproportionately, <b>somebody who knows
something you do not.</b> If a Water-type buff is about to be announced, the traders
who see it first come and take your offers before you can pull them. Your fills are
not a random sample of the market &mdash; they are <em>selected</em>, and selected
against you. Formally, the thing that ruins your day is:</p>
""", '<div class="eq">E[ &Delta;s <span style="font-size:1.2em">|</span> my bid was filled ] '
 '&nbsp;&lt;&nbsp; 0'
 '<span class="where">In words: conditional on somebody having sold to me, the price is '
 'expected to fall. I bought, and then it went down. Every time.</span></div>', """
<p>So a market maker&rsquo;s profit decomposes into two competing pieces:</p>
""", '<div class="eq" style="font-size:11.6pt">P&amp;L &nbsp;=&nbsp; '
 '<span style="color:#006300">spread captured</span> &nbsp;&minus;&nbsp; '
 '<span style="color:#b3302f">adverse selection</span> &nbsp;&minus;&nbsp; '
 '<span style="color:#b3302f">inventory risk</span></div>', """
<p>The first term is small, frequent and reliable. The second is occasional, large
and brutal. Market making is the business of collecting the first faster than the
second takes it away, and every real market maker&rsquo;s technology budget goes on
reducing the second &mdash; which is what all the latency work is ultimately
<em>for</em>. Being fast is not about greed; it is about being able to withdraw your
quotes before the informed trader reaches them.</p>
<div class="key"><span class="lab">The experiment this makes possible</span>
<p>This is why the spec has scheduled &ldquo;meta patches&rdquo; &mdash; announced
shocks like <em>Water types buffed 15%</em>. If we give agents different reaction
latencies, then at each patch the fast agents will systematically profit at the
expense of the slow market maker. And because we control everything, <b>we can
measure exactly how much</b>, in currency, attributable to latency alone. That
number is the most quantitatively interesting result the whole project can produce,
and it is a direct, honest answer to &ldquo;why does anybody care about
nanoseconds?&rdquo;</p></div>

<h3>5.10 &nbsp;Cointegration: trading a relationship instead of a price</h3>
<p>Charmander, Charmeleon and Charizard are the same creature at three stages. Their
prices should be tethered: a Charizard is worth roughly what a Charmander is worth
plus the cost of evolving it. Individually each price wanders unpredictably (5.6,
Fact 2). But their <em>difference</em> should not wander &mdash; it should hover
around a constant and get pulled back whenever it strays.</p>
<p>Two series with that property are <b>cointegrated</b>. In the standard vocabulary,
each price alone is <b>I(1)</b> &mdash; a random walk, non-stationary, no fixed mean
to return to &mdash; but some linear combination of them is <b>I(0)</b>:
stationary, mean-reverting, forecastable.</p>
""", '<div class="eq">spread<sub>t</sub> &nbsp;=&nbsp; P<sup>B</sup><sub>t</sub> &minus; '
 '&beta; &middot; P<sup>A</sup><sub>t</sub><br>'
 'z<sub>t</sub> &nbsp;=&nbsp; ' + frac('spread<sub>t</sub> &minus; &mu;<sub>spread</sub>',
 '&sigma;<sub>spread</sub>') +
 '<span class="where"><b>&beta;</b> the hedge ratio &mdash; how many Charmander one Charizard is '
 'worth, estimated by regressing one price on the other &middot; <b>z</b> the spread restated in '
 'standard deviations</span></div>', """
<p>The strategy is then almost embarrassingly simple: when z climbs above +2, B is
expensive relative to A, so sell B and buy &beta; of A. When z falls below &minus;2,
do the reverse. Close out when z returns to 0.</p>
""", coint_svg(), """
<p>Notice what the arbitrageur is and is not claiming. It has <b>no opinion
whatsoever</b> about whether Charizard is going up or down &mdash; it is short one
and long the other, so a market-wide rally leaves it roughly flat. Its only bet is
that <em>the relationship holds</em>. That is what &ldquo;statistical
arbitrage&rdquo; means, and it is why such strategies can have small, steady returns
that look nothing like those of the underlying instruments.</p>
<div class="flag"><span class="lab">The honest caveat, which is also the risk</span>
<p>Cointegration is an <em>assumption being tested</em>, not a guarantee. The correct
procedure is to run a statistical test &mdash; Engle&ndash;Granger, which is an
augmented Dickey&ndash;Fuller test on the regression residuals &mdash; and confirm
the spread really is stationary before trading it. When a relationship that
&ldquo;always held&rdquo; stops holding, the position that was hedged becomes two
large naked bets at once, and the losses are famous. Our arb bot should therefore
have a stop, and watching it get destroyed by a meta patch that breaks a
relationship is a genuinely instructive thing to build.</p></div>

<h3>5.11 &nbsp;Sharpe ratio: scoring the agents fairly</h3>
<p>Finally, the leaderboard needs a metric, and total P&amp;L is a bad one &mdash; it
rewards whoever took the most risk, which over a short sample mostly rewards
luck. The standard correction is return per unit of risk:</p>
""", '<div class="eq">S &nbsp;=&nbsp; ' + frac('r&#772; &minus; r<sub>f</sub>', '&sigma;<sub>r</sub>')
 + ' &nbsp;&times;&nbsp; &radic;<span class="rad">N</span>'
 '<span class="where"><b>r&#772;</b> mean return per period &middot; <b>r<sub>f</sub></b> the '
 'risk-free rate (zero here) &middot; <b>&sigma;<sub>r</sub></b> its standard deviation &middot; '
 '<b>N</b> periods per year, to annualise</span></div>', """
<p>An agent earning 5% with almost no variance is a better <em>business</em> than one
earning 20% by nearly dying twice, and Sharpe is what says so. The
&radic;N is there because mean return scales with time while standard deviation
scales with &radic;time &mdash; so the ratio itself grows like &radic;N, and you must
divide out the sampling frequency before comparing anything.</p>
<div class="aside"><span class="lab">Where Sharpe lies to you</span>
<p>Sharpe divides by standard deviation, which assumes risk is symmetric and
adequately described by the second moment. We proved in 5.6 that our returns are
fat-tailed &mdash; kurtosis """ + f"{KURT:.2f}" + """ &mdash; so the fourth moment is exactly
where the danger lives, and Sharpe cannot see it. A strategy that earns a little every
day and loses everything once a year posts a magnificent Sharpe right up until it
does not. We should report maximum drawdown alongside it, and being able to say
<em>why</em> is worth more than the ratio itself.</p></div>
""")

# ----------------------------------------------------------------------- part 6
add("""
<h2 class="part"><span class="num">Part 6</span>Making the Pok&eacute;mon layer do
real work</h2>
<p class="standfirst">If the theme is only a coat of paint, it is a liability. If it
drives the price process, it is the best thing about the project. The difference is
deliberate design.</p>

<p>The risk is obvious: an interviewer at a serious firm sees Pok&eacute;mon on a CV
and wonders whether they are looking at a toy. The defence is not to hide the theme
&mdash; it is to make the theme <em>load-bearing</em>, so that every part of it is
standing in for a real market phenomenon that would be harder to demonstrate
otherwise.</p>

<table>
<tr><th style="width:38mm">Themed feature</th><th style="width:52mm">Real mechanism</th>
    <th>What it lets us measure</th></tr>
<tr><td><b>Base stats &rarr; fundamental value</b></td>
    <td>An asset with a knowable fair value</td>
    <td>Whether prices actually converge to fundamentals, and how fast. You cannot do
        this with real equities because nobody knows the true value.</td></tr>
<tr><td><b>Meta patches</b><br><span style="font-size:9pt;color:#898781">&ldquo;Water
    types buffed 15%&rdquo;</span></td>
    <td>A scheduled, quantified information shock</td>
    <td><b>Adverse selection, in currency.</b> Give agents different reaction
        latencies and measure exactly who profits from whom. This is the headline
        result of the whole project.</td></tr>
<tr><td><b>Evolution lines</b></td><td>A cointegrated basket</td>
    <td>Statistical arbitrage that is genuinely justified rather than curve-fitted
        &mdash; we <em>know</em> the relationship exists because we built it.</td></tr>
<tr><td><b>Legendary rarity</b></td><td>A tiny free float</td>
    <td>Slippage and spread as a function of depth, with everything else held
        constant. The cleanest possible microstructure experiment.</td></tr>
<tr><td><b>Type effectiveness</b></td><td>A correlation structure across instruments</td>
    <td>Whether a portfolio-level hedge behaves as the correlation matrix says it
        should.</td></tr>
</table>

<h3>Fundamental value</h3>
<p>Each species gets a fair value that agents can reason about &mdash; some knowing
it precisely, some noisily, some not at all. A workable form:</p>
""", '<div class="eq" style="font-size:11.4pt">V<sub>0</sub> &nbsp;=&nbsp; k &middot; '
 '<span style="font-size:1.15em">(</span>' + frac('BST', '500') +
 '<span style="font-size:1.15em">)</span><sup>&alpha;</sup> &middot; R &middot; T<sub>t</sub>'
 '<span class="where"><b>BST</b> base stat total &mdash; the in-game power number &middot; '
 '<b>&alpha;</b> a convexity exponent around 1.5, so strong species are worth '
 'disproportionately more &middot; <b>R</b> rarity multiplier (common 1, rare ~4, legendary ~40) '
 '&middot; <b>T<sub>t</sub></b> the current meta&rsquo;s demand factor for that type &mdash; '
 '<em>the only time-varying term, and the one a patch moves</em></span></div>', """
<p>To be clear about what this is: these are <b>design choices, not discovered
laws</b>. We are writing the generating process, so we get to pick it. The
interesting question is never whether the formula is &ldquo;right&rdquo; &mdash; it
is whether a market populated by agents with partial, delayed and noisy knowledge of
V<sub>0</sub> produces prices that behave like real ones. That is a real question with
a real answer, and it is answerable precisely <em>because</em> we control the truth.</p>

<div class="key"><span class="lab">How to present this on paper</span>
<p>Put <b>&ldquo;Limit Order Book &amp; Matching Engine&rdquo;</b> on the CV, with the
theme as a parenthetical at most. Lead with the numbers: throughput, p99 latency, the
data-structure change that bought the most. Then let the theme come out in
conversation, where it works entirely in your favour &mdash; it is disarming, it is
memorable, and it gives you a natural bridge into twenty minutes of genuine
microstructure discussion. Theme on paper is a risk; theme in conversation is an
asset.</p></div>
""")

# ----------------------------------------------------------------------- part 7
add("""
<h2 class="part"><span class="num">Part 7</span>What we build, in what order, and
why</h2>
<p class="standfirst">Six sub-projects, each independently shippable. We are starting
the first slice of the first one, and nothing else.</p>

<p>The original spec is four phases and seven stages &mdash; realistically six to ten
weeks. Designing all of it now would mean designing the UI against assumptions the
engine has not yet tested. So it is decomposed, and each piece gets its own design,
plan and build cycle:</p>

<table>
<tr><th style="width:8mm"></th><th style="width:42mm">Sub-project</th>
    <th style="width:54mm">Ships</th><th>Needs</th></tr>
<tr><td class="mono"><b>A</b></td><td><b>Engine core</b> <span class="pill">now</span></td>
    <td>The book, the order types, cancel/modify, a Python reference, fuzz and
        determinism tests</td><td>&mdash;</td></tr>
<tr><td class="mono"><b>B</b></td><td>Flow generator</td>
    <td>Noise and momentum agents; a live tape in the terminal</td><td>A</td></tr>
<tr><td class="mono"><b>C</b></td><td>Optimisation &amp; benchmarks</td>
    <td>v1&ndash;v3, the latency harness, <b>the numbers</b></td><td>A, B</td></tr>
<tr><td class="mono"><b>D</b></td><td>UI &amp; gateway</td>
    <td>WebSocket feed, ladder, tape, candles, latency panel</td><td>A, B</td></tr>
<tr><td class="mono"><b>E</b></td><td>Smart agents</td>
    <td>Avellaneda&ndash;Stoikov maker, arbitrageur, Pok&eacute;mon fundamentals</td>
    <td>B</td></tr>
<tr><td class="mono"><b>F</b></td><td>Analysis</td>
    <td>Stylized facts, impact curve, P&amp;L attribution, the write-up</td><td>E</td></tr>
</table>

<p>Note the critical path to the checkpoint your mentor asked for &mdash; throughput
and latency numbers &mdash; is <b>A &rarr; B &rarr; C</b>. D, E and F are all
valuable and none of them are on it.</p>

<h3>Sub-project A, split in two</h3>
<p>Even A is too much for one green commit, so it ships in two slices. The point of
the split is that <b>A1&rsquo;s tests are what make A2 safe</b> &mdash; the invariant
checks and the Python differential harness are written before the fiddly order types
they are meant to police.</p>

<table>
<tr><th style="width:14mm">Slice</th><th>Contents</th></tr>
<tr><td class="mono"><b>A1</b></td><td>One instrument &middot; limit and market orders
    &middot; cancel &middot; partial fills &middot; price-time priority &middot;
    the replay CLI &middot; the Python reference &middot; determinism and fuzz
    tests. <b>Green, pushed, demoable.</b></td></tr>
<tr><td class="mono"><b>A2</b></td><td>IOC &middot; FOK &middot; post-only &middot;
    modify with the priority rules from Part 3 &middot; self-trade prevention
    &middot; multiple instruments.</td></tr>
</table>

<h3>The shape of the code</h3>
<pre>libpokex/                  <span class="c">static lib &middot; no I/O, no threads, no clock</span>
  types.hpp                <span class="c">Price, Qty, OrderId, Side, Order, Sequence</span>
  events.hpp               <span class="c">commands + events, and their text encoding</span>
  book_concept.hpp         <span class="c">the five operations, documented as a contract</span>
  book_v0_map.hpp          <span class="c">std::map + deque &mdash; the baseline, kept forever</span>
  matching.hpp             <span class="c">template&lt;Book&gt; &mdash; Part 2's loop, written ONCE</span>

pokex-replay               <span class="c">CLI: commands.txt &rarr; events.txt. Pure. Deterministic.</span>
reference/engine.py        <span class="c">an independent reimplementation, same file formats</span>
tests/                     <span class="c">golden &middot; property/fuzz &middot; differential</span>
</pre>

<p>Two decisions in there are worth restating, because they are the ones that pay off
later:</p>
<ul>
<li><b>The engine is a pure function.</b> Commands in, events out. No sockets, no
threads, no database, no clock. Every later sub-project &mdash; the agents, the
WebSocket gateway, even the lock-free ring buffer in v4 &mdash; attaches
<em>around</em> that function without modifying it.</li>
<li><b>The book is swapped at compile time,</b> via
<code>template&lt;typename Book&gt;</code> rather than a virtual base class. A virtual
call is an indirect jump that defeats inlining and pollutes the branch predictor
&mdash; so using one here would mean measuring our own abstraction instead of the
data structure. The same test suite is instantiated against every version, which is
what keeps four implementations from silently drifting apart and quietly invalidating
the benchmark.</li>
</ul>

<h3>How we know it is correct</h3>
<p>Three layers, weakest to strongest:</p>
<ol>
<li><b>Golden tests.</b> Hand-written scenarios with expected event streams. These
catch semantic mistakes &mdash; the maker-price rule, residual handling &mdash; and
they double as documentation of intended behaviour.</li>
<li><b>Property tests / fuzzing.</b> Throw a million random <em>valid</em> command
sequences at the engine and assert the invariants after every single one: the book
never crosses; no order fills more than its size; and for every order,
<code>filled + resting + cancelled == original</code>. Fuzzing does not need to know
the right answer, only what must never happen &mdash; which is why it finds the bugs
you did not think to write a test for.</li>
<li><b>Differential testing.</b> Run the C++ engine and the Python reference on the
same random flows and require byte-identical output. Two independent implementations
agreeing is the strongest correctness evidence available to us, and it is only
possible because of the determinism rule from Part 2.</li>
</ol>

<h3>What &ldquo;done&rdquo; means for A1</h3>
<ul>
<li>Every test green, including a fuzz run of at least a million messages.</li>
<li>The same input file replayed twice produces identical bytes.</li>
<li>C++ and Python agree on ten thousand randomly generated flows.</li>
<li>Clean under AddressSanitizer and UndefinedBehaviorSanitizer.</li>
<li>A README that a stranger can follow to build and run it.</li>
</ul>
<p>Note what is <em>not</em> on that list: any performance number at all. A1 is
allowed to be slow. Making it fast is sub-project C, and doing it in that order is
what makes the eventual speed claim believable rather than decorative.</p>

<div class="key"><span class="lab">The bullet we are working towards</span>
<p>Everything above is in service of being able to write one sentence and defend it
for twenty minutes: <em>&ldquo;Built a limit order book and matching engine in C++;
replaced a std::map book with an array-indexed price ladder, an intrusive free-list
allocator and an open-addressing ID index, taking sustained throughput from X to Y
orders/sec and p99 matching latency from A to B &micro;s on an identical replayed
order flow, verified byte-identical against a Python reference implementation.&rdquo;</em></p>
<p>Note that every number in it is one <b>you measured</b>, and every claim is one
the tests can prove. That is the difference between a project and a portfolio piece.</p></div>
""")

# --------------------------------------------------------------------- glossary
G = [
 ("Adverse selection", "The tendency for your resting orders to be filled precisely by people who know something you do not. The market maker's central risk."),
 ("Aggressive / passive", "An aggressive order trades immediately against the book (a taker). A passive order rests and waits (a maker)."),
 ("Ask (offer)", "A price at which someone is willing to sell. Asks sit above the spread."),
 ("Bid", "A price at which someone is willing to buy. Bids sit below the spread."),
 ("Cache line", "The 64-byte block the CPU always fetches as a unit. Data layout is about getting what you need next into the line you already have."),
 ("Cointegration", "Two individually random-walking prices whose difference is stationary. The basis of statistical arbitrage."),
 ("Crossed book", "An illegal state where the best bid is at or above the best ask. Can exist only for the instant before the engine resolves it."),
 ("Depth", "How much quantity rests at each price level. Determines the cost of trading size."),
 ("Determinism", "Same input bytes produce byte-identical output bytes, always. Requires no clocks, no randomness, no allocation-dependent iteration inside the engine."),
 ("False sharing", "Two threads writing different variables that happen to share a cache line, causing them to fight over it. A slowdown with no logical cause."),
 ("FIFO", "First in, first out. The queue discipline within a single price level."),
 ("Fill-or-kill (FOK)", "Fill the entire quantity immediately or do nothing at all."),
 ("Immediate-or-cancel (IOC)", "Take whatever is available right now; cancel the remainder rather than resting it."),
 ("Intrusive list", "A linked list whose next/prev pointers live inside the element itself, so element and links share a cache line."),
 ("Inventory risk", "The exposure a market maker carries from holding a position it did not want, acquired as a side effect of providing liquidity."),
 ("Kurtosis", "The normalised fourth moment of a distribution. 3 for a Normal; higher means fatter tails. Financial returns are typically 4&ndash;20."),
 ("Limit order", "An order with a price limit. Trades at that price or better, and rests if it cannot."),
 ("Liquidity", "The ability to trade meaningful size, quickly, without moving the price. Has three dimensions: tightness, depth, resiliency."),
 ("Maker / taker", "The maker's order was resting and provided liquidity; the taker arrived and consumed it. Trades print at the maker's price."),
 ("Market order", "An order with no price limit. Always fills, at whatever the book costs."),
 ("Microprice", "A fair-value estimate weighting each side's price by the <em>opposite</em> side's quantity, so book imbalance pulls the estimate."),
 ("Mid", "The average of best bid and best ask. A convention, not a tradeable price."),
 ("Object pool / free list", "Pre-allocating all the memory once and handing it out from a list, so the hot path never calls malloc."),
 ("Open addressing", "A hash table that resolves collisions by probing nearby slots rather than chaining a linked list, avoiding pointer chasing."),
 ("Poisson process", "Events arriving independently at a constant average rate; the gaps between them are exponentially distributed and memoryless."),
 ("Post-only", "An order that must rest. Rejected if it would trade immediately, guaranteeing maker status and the associated rebate."),
 ("Price improvement", "Getting a better price than your limit, because you traded at the resting order's price rather than your own."),
 ("Price-time priority", "The matching rule: better price first, then earlier arrival. Time priority is the payment for patience."),
 ("Rebate", "A small payment exchanges make to makers, funded by a fee on takers, to encourage resting liquidity."),
 ("Reservation price", "In Avellaneda&ndash;Stoikov, the price at which a maker is genuinely indifferent &mdash; the mid, shifted against its own inventory."),
 ("Resiliency", "How quickly the book refills after being consumed. The third and least-discussed dimension of liquidity."),
 ("Self-trade prevention", "Refusing to match a participant against itself, because such trades fake the public volume signal (wash trading)."),
 ("Sharpe ratio", "Excess return divided by its standard deviation, annualised. Blind to fat tails, so quote maximum drawdown alongside it."),
 ("Slippage", "The gap between the price you expected and the volume-weighted price you actually got. The cost of urgency and of size."),
 ("Spread", "Best ask minus best bid. Not a fee &mdash; the price of immediacy."),
 ("SPSC ring buffer", "Single-producer, single-consumer queue. Lets an ingress thread hand work to a matching thread without locks."),
 ("Stylized facts", "Statistical properties found in essentially all real return series: fat tails, volatility clustering, near-zero return autocorrelation."),
 ("Tick", "The smallest permitted price increment. Prices are integer counts of ticks, which is what makes an array-indexed book possible."),
 ("Top of book", "The best bid and best ask together. The only two prices most participants ever look at."),
 ("Volatility", "The standard deviation of returns. Not constant &mdash; it clusters."),
 ("Volatility clustering", "Big moves follow big moves and quiet follows quiet. Visible as positive autocorrelation in |returns|."),
 ("VWAP", "Volume-weighted average price. What a multi-level fill actually cost, in one number."),
 ("Wash trading", "Trading with yourself to manufacture apparent volume. Illegal manipulation; the reason self-trade prevention exists."),
]
add('<h2 class="part"><span class="num">Glossary</span>Every term, one line each</h2>'
    '<p class="standfirst">In alphabetical order, for when you are half-way through '
    'a conversation and need one fast.</p><dl class="gloss">'
    + "".join(f"<dt>{t}</dt><dd>{d}</dd>" for t, d in sorted(G, key=lambda x: x[0].lower()))
    + "</dl>")

# ------------------------------------------------------------------------ render
HTML = ("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<title>Limit Order Book &amp; Matching Engine — Design Primer</title>"
        f"<style>{CSS}</style></head><body><div class=\"sheet\">"
        + "".join(DOC) + "</div></body></html>")

with open("pokeexchange-explainer.html", "w") as f:
    f.write(HTML)
print(f"wrote pokeexchange-explainer.html  ({len(HTML):,} bytes, "
      f"{len(G)} glossary entries, 11 figures)")
