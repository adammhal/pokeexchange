window.PX = window.PX || {};

// Prices arrive as integer ticks. One tick is a cent, so display divides by 100.
// Never do arithmetic in display units.
PX.fmt = {
  px:  function (ticks) { return (ticks / 100).toFixed(2); },
  qty: function (n) { return n.toLocaleString('en-US'); },
  money: function (ticks) {
    var d = ticks / 100;
    var s = Math.abs(d) >= 1000
      ? Math.round(d).toLocaleString('en-US')
      : d.toFixed(2);
    return (d > 0 ? '+' : d < 0 ? '−' : '') + '$' + s.replace('-', '');
  },
  signed: function (n) {
    return (n > 0 ? '+' : n < 0 ? '−' : '') + Math.abs(n).toLocaleString('en-US');
  },
  ns: function (v) {
    if (v >= 1000) return (v / 1000).toFixed(1) + 'µs';
    return Math.round(v) + 'ns';
  },
  big: function (v) {
    if (v >= 1e6) return (v / 1e6).toFixed(1) + 'M';
    if (v >= 1e3) return Math.round(v / 1e3) + 'k';
    return String(v);
  }
};

// Canvases are sized in CSS pixels but drawn at device resolution, otherwise
// every hairline turns into a grey smear on a retina screen.
PX.fitCanvas = function (canvas) {
  var dpr = window.devicePixelRatio || 1;
  var r = canvas.getBoundingClientRect();
  var w = Math.max(1, Math.round(r.width));
  var h = Math.max(1, Math.round(r.height));
  if (canvas.width !== w * dpr || canvas.height !== h * dpr) {
    canvas.width = w * dpr;
    canvas.height = h * dpr;
  }
  var ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  return { ctx: ctx, w: w, h: h };
};

// Crisp 1px lines need to sit on a half-pixel.
PX.hline = function (ctx, x0, x1, y, color) {
  ctx.strokeStyle = color;
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(x0, Math.round(y) + 0.5);
  ctx.lineTo(x1, Math.round(y) + 0.5);
  ctx.stroke();
};
