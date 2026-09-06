window.PX = window.PX || {};

// Candles plus a volume histogram underneath, sharing one price axis. The
// dashed overlay is fundamental value, which is the thing the agents are
// collectively trying to discover.
PX.Candles = (function () {
  var WINDOW = 110;

  function render(canvas, session, upto) {
    var f = PX.fitCanvas(canvas), ctx = f.ctx, W = f.w, H = f.h;
    var C = PX.C;
    var L = 52, R = 14, T = 12;
    var volH = Math.round(H * 0.2);
    var B = H - 22 - volH;
    if (B <= T) return;

    var start = Math.max(0, upto - WINDOW + 1);
    var frames = session.frames.slice(start, upto + 1);
    if (!frames.length) return;

    var lo = Infinity, hi = -Infinity, vmax = 1, i;
    for (i = 0; i < frames.length; i++) {
      var c = frames[i].c;
      if (c[4] > 0 || c[1] > 0) { lo = Math.min(lo, c[2]); hi = Math.max(hi, c[1]); }
      lo = Math.min(lo, frames[i].f); hi = Math.max(hi, frames[i].f);
      vmax = Math.max(vmax, c[4]);
    }
    if (!isFinite(lo)) return;
    var pad = Math.max(2, (hi - lo) * 0.08);
    lo -= pad; hi += pad;

    var step = (W - L - R) / frames.length;
    var bw = Math.max(1.5, Math.min(7, step * 0.62));
    var X = function (k) { return L + step * (k + 0.5); };
    var Y = function (p) { return B - ((p - lo) / (hi - lo)) * (B - T); };

    // price gridlines
    ctx.font = '9px ' + C.mono;
    ctx.fillStyle = C.faint;
    ctx.textAlign = 'right';
    for (i = 0; i <= 3; i++) {
      var p = lo + ((hi - lo) / 3) * i;
      PX.hline(ctx, L, W - R, Y(p), C.grid);
      ctx.fillText(PX.fmt.px(p), L - 8, Y(p) + 3);
    }

    // volume
    var vb = H - 16;
    for (i = 0; i < frames.length; i++) {
      var vol = frames[i].c[4];
      if (!vol) continue;
      var vh = (vol / vmax) * volH;
      var up = frames[i].c[3] >= frames[i].c[0];
      ctx.fillStyle = up ? 'rgba(57,135,229,0.42)' : 'rgba(230,103,103,0.42)';
      ctx.fillRect(X(i) - bw / 2, vb - vh, bw, vh);
    }
    PX.hline(ctx, L, W - R, vb, C.baseline);

    // candles
    for (i = 0; i < frames.length; i++) {
      var k = frames[i].c;
      if (!k[4] && !k[1]) continue;
      var rising = k[3] >= k[0];
      var col = rising ? C.bid : C.ask;
      ctx.strokeStyle = col;
      ctx.fillStyle = col;
      ctx.lineWidth = 1;
      var cx = Math.round(X(i)) + 0.5;
      ctx.beginPath();
      ctx.moveTo(cx, Y(k[1]));
      ctx.lineTo(cx, Y(k[2]));
      ctx.stroke();
      var yo = Y(k[0]), yc = Y(k[3]);
      var top = Math.min(yo, yc);
      ctx.fillRect(X(i) - bw / 2, top, bw, Math.max(1, Math.abs(yc - yo)));
    }

    // fundamental value
    ctx.save();
    ctx.setLineDash([4, 3]);
    ctx.strokeStyle = C.ink2;
    ctx.globalAlpha = 0.7;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (i = 0; i < frames.length; i++) {
      var y = Y(frames[i].f);
      if (i === 0) ctx.moveTo(X(i), y); else ctx.lineTo(X(i), y);
    }
    ctx.stroke();
    ctx.restore();

    // last price marker
    var lastClose = frames[frames.length - 1].c[3];
    if (lastClose) {
      ctx.fillStyle = C.ink;
      ctx.fillRect(W - R - 3, Y(lastClose) - 1, 3, 2);
    }
  }
  return { render: render };
})();
