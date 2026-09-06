window.PX = window.PX || {};

// The order book ladder. DOM rather than canvas, because the text has to be
// pixel-crisp and because a CSS width transition gives smooth bar animation
// between frames for free.
PX.Ladder = (function () {
  var ROWS = 13;

  function build(container) {
    var rows = [];
    for (var i = 0; i < ROWS; i++) {
      var row = document.createElement('div');
      row.className = 'lrow';
      var px = document.createElement('span'); px.className = 'lpx';
      var bar = document.createElement('div'); bar.className = 'lbar';
      var fill = document.createElement('i');
      bar.appendChild(fill);
      var qty = document.createElement('span'); qty.className = 'lqty';
      row.appendChild(px); row.appendChild(bar); row.appendChild(qty);
      container.appendChild(row);
      rows.push({ row: row, px: px, fill: fill, qty: qty });
    }
    return rows;
  }

  function init() {
    return {
      asks: build(document.getElementById('asks')),
      bids: build(document.getElementById('bids'))
    };
  }

  // levels[0] is always the best price. Asks render bottom-up so the best ask
  // sits against the spread band; bids render top-down for the same reason.
  function paint(slots, levels, scale, reverse) {
    var n = slots.length;
    for (var i = 0; i < n; i++) {
      var slot = slots[reverse ? n - 1 - i : i];
      var lvl = levels[i];
      if (!lvl) {
        slot.px.textContent = '';
        slot.qty.textContent = '';
        slot.fill.style.width = '0%';
        slot.row.classList.remove('top');
        continue;
      }
      slot.px.textContent = PX.fmt.px(lvl[0]);
      slot.qty.textContent = PX.fmt.qty(lvl[1]);
      slot.fill.style.width = Math.max(1.2, (lvl[1] / scale) * 100).toFixed(1) + '%';
      slot.row.classList.toggle('top', i === 0);
    }
  }

  function render(state, frame) {
    var bids = frame.b || [], asks = frame.a || [];
    var scale = 1;
    var i;
    for (i = 0; i < bids.length && i < ROWS; i++) scale = Math.max(scale, bids[i][1]);
    for (i = 0; i < asks.length && i < ROWS; i++) scale = Math.max(scale, asks[i][1]);

    paint(state.asks, asks, scale, true);
    paint(state.bids, bids, scale, false);

    var mid = (frame.bb && frame.ba) ? (frame.bb + frame.ba) / 2 : (frame.bb || frame.ba || 0);
    document.getElementById('ladderMid').textContent = mid ? PX.fmt.px(mid) : '—';
    document.getElementById('ladderSpread').textContent =
      (frame.bb && frame.ba) ? (frame.ba - frame.bb) + 't' : '—';

    var bq = 0, aq = 0;
    for (i = 0; i < bids.length; i++) bq += bids[i][1];
    for (i = 0; i < asks.length; i++) aq += asks[i][1];
    document.getElementById('bookTotals').textContent =
      PX.fmt.qty(bq) + ' × ' + PX.fmt.qty(aq) + ' shown';
  }

  return { init: init, render: render };
})();
