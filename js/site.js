// theme toggle, remembered per browser
(function () {
  var b = document.getElementById('themeBtn');
  if (!b) return;
  var root = document.documentElement;
  function sync() { b.setAttribute('aria-pressed', root.dataset.theme === 'dark' ? 'true' : 'false'); }
  sync();
  b.addEventListener('click', function () {
    root.dataset.theme = root.dataset.theme === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('ds-theme', root.dataset.theme); } catch (e) {}
    sync();
  });
})();

(function () {
  var calm = matchMedia('(prefers-reduced-motion: reduce)').matches;

  // headings: wrap each word so it can slide out of a mask. keeps inner elements (the "no vm." pill) whole
  document.querySelectorAll('[data-split]').forEach(function (h) {
    var k = 0, out = [];
    h.childNodes.forEach(function (n) {
      if (n.nodeType === 3) {
        n.textContent.split(/(\s+)/).forEach(function (t) {
          if (!t) return;
          if (/^\s+$/.test(t)) { out.push(document.createTextNode(' ')); return; }
          out.push(wrap(document.createTextNode(t)));
        });
      } else out.push(wrap(n.cloneNode(true)));
    });
    function wrap(node) {
      var w = document.createElement('span'), i = document.createElement('span');
      w.className = 'w'; i.style.setProperty('--k', k++); i.appendChild(node); w.appendChild(i);
      return w;
    }
    h.setAttribute('aria-label', h.textContent.replace(/\s+/g, ' ').trim());
    h.textContent = '';
    out.forEach(function (n) { h.appendChild(n); });
  });

  var targets = document.querySelectorAll('[data-rv], [data-split]');
  if (calm || !('IntersectionObserver' in window)) {
    targets.forEach(function (el) { el.classList.add('in'); });
  } else {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) {
        if (!e.isIntersecting) return;
        e.target.classList.add('in');
        io.unobserve(e.target);
        var c = e.target.querySelector('[data-count]');
        if (c) countDown(c);
      });
    }, { rootMargin: '0px 0px -12% 0px' });
    targets.forEach(function (el) { io.observe(el); });
  }

  // "0 VMs" counts down from a few, because there are none
  function countDown(el) {
    var n = +el.dataset.count;
    el.textContent = n;
    var t = setInterval(function () { el.textContent = --n; if (n <= 0) clearInterval(t); }, 260);
  }

  if (calm) return;

  // ticker tapes: run on their own, speed up and flip with scroll
  var tracks = [].map.call(document.querySelectorAll('.band .track'), function (t, i) {
    return t.animate([{ transform: 'translateX(0)' }, { transform: 'translateX(-50%)' }],
      { duration: 38000 + i * 6000, iterations: Infinity, direction: i ? 'reverse' : 'normal' });
  });

  var pieces = [].slice.call(document.querySelectorAll('.collage .pc'));
  var bar = document.querySelector('.progress i');
  var pin = document.querySelector('.road-pin');
  var road = pin && pin.querySelector('.road');
  var wide = matchMedia('(min-width: 900px)');
  var lastY = scrollY, vel = 0, ticking = false;

  function layoutPin() {
    if (!pin) return;
    if (!wide.matches) { pin.classList.remove('pinned'); pin.style.height = ''; road.style.transform = ''; return; }
    pin.classList.add('pinned');
    var extra = road.scrollWidth - innerWidth;
    pin.style.height = (innerHeight + Math.max(extra, 0)) + 'px';
  }

  function frame() {
    ticking = false;
    var y = scrollY, h = document.documentElement.scrollHeight - innerHeight;
    vel = vel * .8 + (y - lastY) * .2; lastY = y;
    if (bar) bar.style.setProperty('--p', h > 0 ? y / h : 0);
    pieces.forEach(function (p) {
      var d = +p.dataset.depth;
      p.style.transform = 'translate3d(0,' + (-y * d * .18).toFixed(1) + 'px,0) rotate(' + (y * d * .01).toFixed(2) + 'deg)';
    });
    var rate = Math.min(6, 1 + Math.abs(vel) * .25);
    tracks.forEach(function (a, i) { a.playbackRate = (vel < -0.5 ? -1 : 1) * rate; });
    if (pin && pin.classList.contains('pinned')) {
      var r = pin.getBoundingClientRect(), span = pin.offsetHeight - innerHeight;
      var t = Math.min(1, Math.max(0, -r.top / (span || 1)));
      road.style.transform = 'translate3d(' + (-t * Math.max(0, road.scrollWidth - innerWidth)).toFixed(1) + 'px,0,0)';
    }
    if (Math.abs(vel) > .3) request();
  }
  function request() { if (!ticking) { ticking = true; requestAnimationFrame(frame); } }

  addEventListener('scroll', request, { passive: true });
  addEventListener('resize', function () { layoutPin(); request(); });
  layoutPin(); request();
})();
