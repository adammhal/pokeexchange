window.PX = window.PX || {};

// Wires the panels to a recorded session and drives playback.
//
// The session is a recording rather than a live socket, which is what lets this
// run as a static page with no server. A WebSocket source would slot in here
// behind the same frame schema.
(function () {
  var session = window.POKEX_SESSION;
  var bench = window.POKEX_BENCH;

  if (!session || !session.frames || !session.frames.length) {
    document.body.innerHTML =
      '<div style="font:13px/1.6 monospace;color:#c3c2b7;padding:40px">' +
      'No session data.<br><br>Generate it with:<br><br>' +
      '<code>cmake --build build -j</code><br>' +
      '<code>./build/pokex-sim --seed 42 --ticks 60000 --out ui/data/session.js</code>' +
      '</div>';
    return;
  }

  var frames = session.frames;
  var total = frames.length;
  var cursor = 0;
  var playing = true;
  var speed = 1;
  var acc = 0;
  var lastT = 0;
  var BASE_FPS = 12;   // frames of the recording per second at 1x

  var ladder = PX.Ladder.init();
  var cCandles = document.getElementById('cCandles');
  var cDepth = document.getElementById('cDepth');

  var elPx = document.getElementById('lastPx');
  var elChg = document.getElementById('lastChg');
  var elBid = document.getElementById('mBid');
  var elAsk = document.getElementById('mAsk');
  var elSpread = document.getElementById('mSpread');
  var elFund = document.getElementById('mFund');
  var elNo = document.getElementById('frameNo');
  var scrub = document.getElementById('scrub');

  document.getElementById('instrument').textContent = session.meta.instrument;
  document.getElementById('frameTot').textContent = total;
  scrub.max = String(total - 1);

  var openPx = frames[0].c[0] || frames[0].f;

  document.getElementById('agentNote').textContent =
    PX.fmt.big(session.meta.orders + session.meta.cancels) + ' messages · ' +
    Math.round(100 * session.meta.cancels / (session.meta.orders + session.meta.cancels)) +
    '% cancels · ' + PX.fmt.big(session.meta.trades) + ' trades · ' +
    session.meta.self_trades + ' self-trades · ' + session.meta.rejects + ' rejects';

  PX.Latency.build(bench);

  function head(frame) {
    var last = frame.c[3] || frame.bb || frame.ba || openPx;
    elPx.textContent = PX.fmt.px(last);
    var d = ((last - openPx) / openPx) * 100;
    elChg.textContent = (d >= 0 ? '+' : '−') + Math.abs(d).toFixed(2) + '%';
    elChg.className = 'chg ' + (d >= 0 ? 'up' : 'down');
    elBid.textContent = frame.bb ? PX.fmt.px(frame.bb) : '—';
    elAsk.textContent = frame.ba ? PX.fmt.px(frame.ba) : '—';
    elSpread.textContent = (frame.bb && frame.ba) ? (frame.ba - frame.bb) + 't' : '—';
    elFund.textContent = PX.fmt.px(frame.f);
  }

  function draw(seeking) {
    var frame = frames[cursor];
    head(frame);
    PX.Ladder.render(ladder, frame);
    PX.Depth.render(cDepth, frame);
    PX.Candles.render(cCandles, session, cursor);
    PX.Board.render(session, frame);
    PX.Tape.render(frame, seeking);
    elNo.textContent = cursor + 1;
    scrub.value = String(cursor);
  }

  function tick(now) {
    if (!lastT) lastT = now;
    var dt = Math.min(240, now - lastT);
    lastT = now;
    if (playing) {
      acc += (dt / 1000) * BASE_FPS * speed;
      var advance = Math.floor(acc);
      if (advance > 0) {
        acc -= advance;
        // On fast speeds, skip the intermediate frames rather than the drawing,
        // but still collect their prints so the tape does not lie.
        for (var k = 1; k < advance; k++) {
          var mid = (cursor + k) % total;
          if (frames[mid].tp && frames[mid].tp.length) PX.Tape.render(frames[mid], true);
        }
        cursor = (cursor + advance) % total;
        draw(false);
      }
    }
    requestAnimationFrame(tick);
  }

  // ── controls
  var btn = document.getElementById('btnPlay');
  var icon = document.getElementById('playIcon');
  function setPlaying(v) {
    playing = v;
    icon.innerHTML = v ? '&#9616;&#9616;' : '&#9654;';
    btn.setAttribute('aria-pressed', String(v));
  }
  btn.addEventListener('click', function () { setPlaying(!playing); });

  Array.prototype.forEach.call(document.querySelectorAll('.spd'), function (b) {
    b.addEventListener('click', function () {
      speed = parseFloat(b.dataset.speed);
      Array.prototype.forEach.call(document.querySelectorAll('.spd'), function (o) {
        o.classList.toggle('is-on', o === b);
      });
    });
  });

  scrub.addEventListener('input', function () {
    cursor = parseInt(scrub.value, 10) || 0;
    PX.Tape.reset();
    draw(true);
  });

  document.addEventListener('keydown', function (e) {
    if (e.code === 'Space') { e.preventDefault(); setPlaying(!playing); }
    if (e.code === 'ArrowRight') { cursor = (cursor + 1) % total; draw(true); }
    if (e.code === 'ArrowLeft') { cursor = (cursor - 1 + total) % total; draw(true); }
  });

  var resizeTimer = null;
  window.addEventListener('resize', function () {
    clearTimeout(resizeTimer);
    resizeTimer = setTimeout(function () { draw(true); PX.Latency.draw(); }, 90);
  });

  // Fonts change text metrics, so redraw once they land.
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(function () { draw(true); PX.Latency.draw(); });
  }

  // #f=600 parks the session on one frame, paused. Used for screenshots, and
  // handy for linking someone to a specific moment in the session.
  var pinned = /[#&]f=(\d+)/.exec(location.hash || '');
  setPlaying(!pinned);
  if (pinned) {
    cursor = Math.max(0, Math.min(total - 1, parseInt(pinned[1], 10)));
    for (var w = Math.max(0, cursor - 8); w < cursor; w++) PX.Tape.render(frames[w], true);
  }
  draw(true);
  PX.Latency.draw();
  requestAnimationFrame(tick);

  // Exposed so a screenshot run can park the session at a known frame.
  window.PX.seek = function (i) {
    cursor = Math.max(0, Math.min(total - 1, i | 0));
    setPlaying(false);
    PX.Tape.reset();
    // Rebuild the tape from a few preceding frames so it is not empty in a shot.
    for (var k = Math.max(0, cursor - 6); k < cursor; k++) PX.Tape.render(frames[k], true);
    draw(true);
    PX.Latency.draw();
  };
})();
