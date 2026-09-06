window.PX = window.PX || {};

// Engine latency, one small multiple per book version on a shared log axis.
// Never two y-scales on one chart, and never a linear axis for a heavy-tailed
// distribution.
PX.Latency = (function () {
  var canvases = [];
  var scenario = null;

  function pick(bench) {
    if (!bench || !bench.scenarios || !bench.scenarios.length) return null;
    for (var i = 0; i < bench.scenarios.length; i++)
      if (bench.scenarios[i].name === 'deep') return bench.scenarios[i];
    return bench.scenarios[0];
  }

  function build(bench) {
    scenario = pick(bench);
    var grid = document.getElementById('latGrid');
    if (!scenario) {
      grid.innerHTML = '<div class="lat-cell"><div class="lat-sub">no bench.js found. ' +
        'run: pokex-bench --json ui/data/bench.js</div></div>';
      return;
    }
    var html = '';
    for (var i = 0; i < scenario.versions.length; i++) {
      var v = scenario.versions[i];
      var gain = v.speedup_vs_v0;
      var col = i === 0 ? PX.C.muted : (gain >= 1.05 ? PX.C.good : gain <= 0.95 ? PX.C.bad : PX.C.muted);
      html +=
        '<div class="lat-cell">' +
          '<div class="lat-top">' +
            '<span class="lat-name">' + v.name.split(/\s+/)[0] + ' ' +
              v.name.split(/\s+/).slice(1).join(' ') + '</span>' +
            '<span class="lat-gain" style="color:' + col + '">' +
              (i === 0 ? 'baseline' : gain.toFixed(2) + '×') + '</span>' +
          '</div>' +
          '<div class="lat-sub">' + PX.fmt.big(v.throughput_per_sec) + '/s' +
            ' · mean ' + PX.fmt.ns(v.mean_ns) +
            ' · p99.9 ' + PX.fmt.ns(v.p999_ns) + '</div>' +
          '<canvas data-lat="' + i + '"></canvas>' +
        '</div>';
    }
    grid.innerHTML = html;
    canvases = [];
    for (var k = 0; k < scenario.versions.length; k++)
      canvases.push(grid.querySelector('[data-lat="' + k + '"]'));

    document.getElementById('latNote').textContent =
      scenario.description + ' · ' + PX.fmt.big(bench.messages_per_scenario) +
      ' messages · clock granularity ~' + bench.clock_granularity_ns + 'ns, so low ' +
      'percentiles sit on the resolution floor';
  }

  function draw() {
    if (!scenario) return;
    // One shared log-x range across all four, otherwise the panels cannot be
    // compared by eye, which is the entire point of a small multiple.
    var loNs = 1e9, hiNs = 0, i, j;
    for (i = 0; i < scenario.versions.length; i++) {
      var h = scenario.versions[i].histogram;
      for (j = 0; j < h.length; j++) {
        if (h[j][0] > 0) loNs = Math.min(loNs, h[j][0]);
        hiNs = Math.max(hiNs, h[j][1]);
      }
    }
    if (hiNs <= loNs) return;
    var lo = Math.log10(Math.max(20, loNs)), hi = Math.log10(hiNs);

    for (i = 0; i < canvases.length; i++) {
      var f = PX.fitCanvas(canvases[i]);
      var ctx = f.ctx, W = f.w, Hh = f.h;
      var B = Hh - 12, T = 2;
      if (B <= T) continue;
      var hist = scenario.versions[i].histogram;
      var peak = 1;
      for (j = 0; j < hist.length; j++) peak = Math.max(peak, hist[j][2]);

      var X = function (ns) { return ((Math.log10(Math.max(ns, 1)) - lo) / (hi - lo)) * W; };
      var col = i === 0 ? PX.C.muted : PX.C.agents[0];

      ctx.fillStyle = col;
      for (j = 0; j < hist.length; j++) {
        var x0 = X(hist[j][0]), x1 = X(hist[j][1]);
        var bh = (hist[j][2] / peak) * (B - T);
        if (bh < 0.7) continue;
        ctx.fillRect(x0, B - bh, Math.max(1, x1 - x0 - 0.5), bh);
      }
      PX.hline(ctx, 0, W, B, PX.C.baseline);

      ctx.font = '8px ' + PX.C.mono;
      ctx.fillStyle = PX.C.faint;
      ctx.textAlign = 'center';
      [100, 1000, 10000, 100000].forEach(function (t) {
        if (t < Math.pow(10, lo) || t > hiNs) return;
        ctx.fillText(PX.fmt.ns(t), X(t), Hh - 2);
      });
    }
  }

  return { build: build, draw: draw };
})();
