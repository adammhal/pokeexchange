window.PX = window.PX || {};

// Cumulative depth. A staircase, not a smooth curve: the book is discrete, and
// drawing it smooth would imply liquidity exists at prices where it does not.
PX.Depth = (function () {
  function render(canvas, frame) {
    var f = PX.fitCanvas(canvas), ctx = f.ctx, W = f.w, H = f.h;
    var C = PX.C;
    var L = 52, R = 14, T = 20, B = H - 26;
    if (B <= T) return;

    var bids = frame.b || [], asks = frame.a || [];
    if (!bids.length && !asks.length) return;

    var cb = [], ca = [], run = 0, i;
    for (i = 0; i < bids.length; i++) { run += bids[i][1]; cb.push([bids[i][0], run]); }
    run = 0;
    for (i = 0; i < asks.length; i++) { run += asks[i][1]; ca.push([asks[i][0], run]); }

    var lo = cb.length ? cb[cb.length - 1][0] : ca[0][0];
    var hi = ca.length ? ca[ca.length - 1][0] : cb[0][0];
    if (hi <= lo) { hi = lo + 1; }
    var qmax = Math.max(cb.length ? cb[cb.length - 1][1] : 0,
                        ca.length ? ca[ca.length - 1][1] : 0) || 1;

    var X = function (p) { return L + ((p - lo) / (hi - lo)) * (W - L - R); };
    var Y = function (q) { return B - (q / qmax) * (B - T); };

    // gridlines and quantity axis
    ctx.font = '9px ' + C.mono;
    ctx.fillStyle = C.faint;
    ctx.textAlign = 'right';
    for (i = 0; i <= 2; i++) {
      var q = (qmax / 2) * i;
      PX.hline(ctx, L, W - R, Y(q), i === 0 ? C.baseline : C.grid);
      ctx.fillText(PX.fmt.big(Math.round(q)), L - 8, Y(q) + 3);
    }

    function stair(pts, color, soft, dir) {
      if (pts.length < 1) return;
      ctx.beginPath();
      ctx.moveTo(X(pts[0][0]), Y(pts[0][1]));
      for (var k = 0; k < pts.length; k++) {
        var nextP = (k + 1 < pts.length) ? pts[k + 1][0] : pts[k][0] + dir;
        ctx.lineTo(X(nextP), Y(pts[k][1]));
        if (k + 1 < pts.length) ctx.lineTo(X(nextP), Y(pts[k + 1][1]));
      }
      var endX = ctx.currentX;
      ctx.save();
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.stroke();
      // fill down to the baseline
      var last = (pts.length ? pts[pts.length - 1][0] + dir : pts[0][0]);
      ctx.lineTo(X(last), B);
      ctx.lineTo(X(pts[0][0]), B);
      ctx.closePath();
      ctx.fillStyle = soft;
      ctx.fill();
      ctx.restore();
      void endX;
    }

    stair(cb, C.bid, C.bidSoft, -1);
    stair(ca, C.ask, C.askSoft, 1);

    // the untraded gap
    if (frame.bb && frame.ba) {
      ctx.fillStyle = 'rgba(255,255,255,0.045)';
      ctx.fillRect(X(frame.bb), T, Math.max(1, X(frame.ba) - X(frame.bb)), B - T);
    }

    PX.hline(ctx, L, W - R, B, C.baseline);
    ctx.textAlign = 'center';
    ctx.fillStyle = C.faint;
    [lo, (lo + hi) / 2, hi].forEach(function (p) {
      ctx.fillText(PX.fmt.px(p), X(p), B + 15);
    });
  }
  return { render: render };
})();
