window.PX = window.PX || {};

// The trade tape. Coloured by aggressor, which is the only thing about a print
// that tells you who was in a hurry.
PX.Tape = (function () {
  var MAX = 22;
  var rows = [];

  function render(frame, seeking) {
    var el = document.getElementById('tape');
    var prints = frame.tp || [];
    // Newest first.
    for (var i = prints.length - 1; i >= 0; i--) rows.unshift(prints[i]);
    if (rows.length > MAX) rows.length = MAX;

    var html = '';
    for (var k = 0; k < rows.length; k++) {
      var p = rows[k];
      var buy = p[2] === 1;
      html += '<div class="trow ' + (buy ? 'buy' : 'sell') +
              '" style="animation-delay:' + (seeking ? 0 : Math.min(k * 8, 80)) + 'ms">' +
              '<span class="tp">' + PX.fmt.px(p[0]) + '</span>' +
              '<span class="tq">' + PX.fmt.qty(p[1]) + '</span>' +
              '<span class="ts">' + (buy ? 'buy' : 'sell') + '</span></div>';
    }
    el.innerHTML = html;
  }

  function reset() { rows = []; }
  return { render: render, reset: reset };
})();
