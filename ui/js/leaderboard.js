window.PX = window.PX || {};

// Agent P&L and inventory. Inventory gets a centre-zero axis because it is
// signed: a bar growing from the left would make "short 500" look like a small
// positive number.
PX.Board = (function () {
  var built = false;
  var cells = [];

  function build(names) {
    var el = document.getElementById('board');
    var html = '';
    for (var i = 0; i < names.length; i++) {
      var col = PX.C.agents[i % PX.C.agents.length];
      html +=
        '<div class="brow">' +
          '<div class="bname"><i style="background:' + col + '"></i>' + names[i] + '</div>' +
          '<div class="bpnl" data-pnl="' + i + '">—</div>' +
          '<div class="binv">' +
            '<span class="binv-lab">inv</span>' +
            '<span class="binv-track"><i data-inv="' + i +
              '" style="background:' + col + '"></i></span>' +
            '<span class="binv-val" data-invv="' + i + '">—</span>' +
          '</div>' +
        '</div>';
    }
    el.innerHTML = html;
    cells = names.map(function (_, i) {
      return {
        pnl: el.querySelector('[data-pnl="' + i + '"]'),
        inv: el.querySelector('[data-inv="' + i + '"]'),
        invv: el.querySelector('[data-invv="' + i + '"]')
      };
    });
    built = true;
  }

  function render(session, frame) {
    if (!built) build(session.meta.agents);
    var limit = session.meta.position_limit || 600;

    for (var i = 0; i < cells.length; i++) {
      var pnl = frame.p[i], inv = frame.i[i];
      var c = cells[i];
      c.pnl.textContent = PX.fmt.money(pnl);
      c.pnl.className = 'bpnl ' + (pnl > 0 ? 'up' : pnl < 0 ? 'down' : '');

      var frac = Math.max(-1, Math.min(1, inv / limit));
      var half = Math.abs(frac) * 50;
      c.inv.style.width = Math.max(0.6, half) + '%';
      c.inv.style.left = frac >= 0 ? '50%' : (50 - half) + '%';
      c.invv.textContent = PX.fmt.signed(inv);
    }
  }
  return { render: render };
})();
