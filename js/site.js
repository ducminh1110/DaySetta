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

// devlog tag filter
(function () {
  var btns = document.querySelectorAll('.f-btn');
  if (!btns.length) return;
  var cards = document.querySelectorAll('.log-list .card');
  btns.forEach(function (b) {
    b.addEventListener('click', function () {
      var f = b.dataset.f;
      btns.forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      cards.forEach(function (c) { c.hidden = f !== 'all' && c.dataset.tag !== f; });
    });
  });
})();

(function () {
  var root = document.documentElement;
  var calm = matchMedia('(prefers-reduced-motion: reduce)').matches;
  var fine = matchMedia('(pointer: fine)').matches;
  var clamp = function (v, a, b) { return v < a ? a : v > b ? b : v; };

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

  // the lab terminal types its lines when it scrolls in
  var term = document.querySelector('.term');
  var lines = term ? [].slice.call(term.querySelectorAll('.feed li')) : [];
  if (term && !calm) {
    root.classList.add('js-type');
    lines.forEach(function (li) { var t = li.querySelector('.tl'); t.dataset.full = t.textContent; t.textContent = ''; });
  }
  function typeFeed() {
    var i = 0;
    (function next() {
      if (i >= lines.length) return;
      var li = lines[i++], t = li.querySelector('.tl'), full = t.dataset.full, n = 0;
      li.classList.add('shown', 'typing');
      (function step() {
        n += 2 + (Math.random() * 3 | 0);
        t.textContent = full.slice(0, n);
        if (n < full.length) setTimeout(step, 16);
        else { li.classList.remove('typing'); setTimeout(next, 160); }
      })();
    })();
  }

  // short text decodes like a terminal on reveal (stat numbers)
  var GLYPHS = '#%&*+=<>/\\|01';
  function scramble(el) {
    var full = el.textContent, f = 0, total = 18;
    (function step() {
      f++;
      el.textContent = full.split('').map(function (ch, i) {
        if (ch === ' ' || i < (f / total) * full.length) return ch;
        return GLYPHS[Math.random() * GLYPHS.length | 0];
      }).join('');
      if (f < total) requestAnimationFrame(step); else el.textContent = full;
    })();
  }

  // "0 VMs" counts down from a few, because there are none
  function countDown(el) {
    var n = +el.dataset.count;
    el.textContent = n;
    var t = setInterval(function () { el.textContent = --n; if (n <= 0) clearInterval(t); }, 260);
  }

  var targets = document.querySelectorAll('[data-rv], [data-split], .foot-word');
  function reveal(el) {
    el.classList.add('in');
    if (calm) return;
    var c = el.querySelector('[data-count]');
    if (c) countDown(c);
    else if (el.classList.contains('stat')) scramble(el.querySelector('b'));
    if (el === term) typeFeed();
  }
  if (calm || !('IntersectionObserver' in window)) {
    targets.forEach(function (el) { el.classList.add('in'); });
  } else {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) { if (e.isIntersecting) { reveal(e.target); io.unobserve(e.target); } });
    }, { rootMargin: '0px 0px -12% 0px' });
    targets.forEach(function (el) { io.observe(el); });
  }

  // docs: highlight the section you are reading in the right-hand list
  var tocLinks = document.querySelectorAll('.doc-toc a');
  if (tocLinks.length && 'IntersectionObserver' in window) {
    var tio = new IntersectionObserver(function (es) {
      es.forEach(function (e) {
        if (!e.isIntersecting) return;
        tocLinks.forEach(function (a) { a.classList.toggle('on', a.getAttribute('href') === '#' + e.target.id); });
      });
    }, { rootMargin: '-20% 0px -70% 0px' });
    document.querySelectorAll('.prose h2[id]').forEach(function (h) { tio.observe(h); });
  }

  if (calm) return;

  // smooth wheel scrolling on desktop; touch keeps native scrolling
  var lenis = null;
  if (fine && window.Lenis) {
    lenis = new Lenis({ lerp: .1 });
    document.querySelectorAll('a[href^="#"]').forEach(function (a) {
      a.addEventListener('click', function (e) {
        var t = a.getAttribute('href').length > 1 && document.querySelector(a.getAttribute('href'));
        if (t) { e.preventDefault(); lenis.scrollTo(t, { offset: -90 }); }
      });
    });
  }

  // hero headline: every letter thins and lifts as the cursor comes near
  var chars = [];
  document.querySelectorAll('.hero h1 .w > span').forEach(function (sp) {
    (function letterize(node) {
      [].slice.call(node.childNodes).forEach(function (n) {
        if (n.nodeType === 3) {
          var frag = document.createDocumentFragment();
          n.textContent.split('').forEach(function (c) {
            var e = document.createElement('span'); e.className = 'ch'; e.textContent = c; frag.appendChild(e); chars.push(e);
          });
          node.replaceChild(frag, n);
        } else letterize(n);
      });
    })(sp);
  });

  // tilt + glare on the solid blocks
  if (fine) document.querySelectorAll('.card, .stat, .step, .box, .pn a').forEach(function (el) {
    el.classList.add('tilt');
    el.addEventListener('pointermove', function (e) {
      if (el.hasAttribute('data-rv') && !el.classList.contains('in')) return;
      var r = el.getBoundingClientRect(), px = (e.clientX - r.left) / r.width, py = (e.clientY - r.top) / r.height;
      el.style.transition = 'transform .12s ease-out';
      el.style.transform = 'perspective(900px) rotateX(' + ((.5 - py) * 9).toFixed(2) + 'deg) rotateY(' + ((px - .5) * 11).toFixed(2) + 'deg) translateZ(0)';
      el.style.setProperty('--gx', (px * 100).toFixed(1) + '%'); el.style.setProperty('--gy', (py * 100).toFixed(1) + '%'); el.style.setProperty('--go', 1);
    });
    el.addEventListener('pointerleave', function () {
      el.style.transition = 'transform .6s cubic-bezier(.2,.9,.25,1.15)'; el.style.transform = ''; el.style.setProperty('--go', 0);
    });
  });

  // ticker tapes: run on their own, speed up and flip with scroll
  var tracks = [].map.call(document.querySelectorAll('.band .track'), function (t, i) {
    return t.animate([{ transform: 'translateX(0)' }, { transform: 'translateX(-50%)' }],
      { duration: 38000 + i * 6000, iterations: Infinity, direction: i ? 'reverse' : 'normal' });
  });

  // hero collage: parallax, drag-and-throw, and on scroll the three logo pieces snap into the mark
  var cur = document.querySelector('.cursor');
  var collage = document.querySelector('.collage');
  var hero = document.querySelector('.hero');
  var pieces = collage ? [].slice.call(collage.querySelectorAll('.pc')).map(function (el) {
    var pi = el.querySelector('.pi');
    return { el: el, depth: +el.dataset.depth, r: parseFloat(pi.style.getPropertyValue('--r')) || 0,
      role: (el.className.match(/k-(stem|sun|rings)/) || [])[1], x: 0, y: 0, vx: 0, vy: 0, held: false };
  }) : [];

  function markTargets() {
    // the D: stem u x 3u, two quarters of radius 1.5u, centred in the collage
    var W = collage.offsetWidth, u = W * .17, L = (W - 2.5 * u) / 2, T = (W - 3 * u) / 2 - W * .04;
    return {
      stem: { x: L, y: T, w: u, h: 3 * u },
      sun: { x: L + u, y: T, w: 1.5 * u, h: 1.5 * u },
      rings: { x: L + u, y: T + 1.5 * u, w: 1.5 * u, h: 1.5 * u }
    };
  }

  if (collage) {
    var grabbed = null, last = null;
    collage.addEventListener('pointerdown', function (e) {
      var el = e.target.closest('.pc');
      if (!el || el.classList.contains('back')) return;
      grabbed = pieces.find(function (p) { return p.el === el; });
      grabbed.held = true; el.classList.add('held');
      el.setPointerCapture(e.pointerId);
      last = { x: e.clientX, y: e.clientY, t: performance.now() };
      e.preventDefault();
    });
    collage.addEventListener('pointermove', function (e) {
      if (!grabbed) return;
      var now = performance.now(), dt = Math.max(1, now - last.t);
      var dx = e.clientX - last.x, dy = e.clientY - last.y;
      grabbed.x += dx; grabbed.y += dy;
      grabbed.vx = dx / dt * 16; grabbed.vy = dy / dt * 16;
      last = { x: e.clientX, y: e.clientY, t: now };
    });
    var drop = function () {
      if (!grabbed) return;
      grabbed.held = false; grabbed.el.classList.remove('held'); grabbed = null;
      if (cur) cur.classList.remove('grab');
    };
    collage.addEventListener('pointerup', drop);
    collage.addEventListener('pointercancel', drop);
  }

  // cursor dot

  var mx = -100, my = -100, cx = -100, cy = -100;
  if (cur && fine) {
    root.classList.add('has-cursor');
    addEventListener('pointermove', function (e) {
      mx = e.clientX; my = e.clientY;
      var t = e.target;
      cur.classList.toggle('grab', !!(t.closest && t.closest('.pc.held')));
      cur.classList.toggle('drag', !!(t.closest && t.closest('[data-drag] .pc:not(.back)')) && !cur.classList.contains('grab'));
      cur.classList.toggle('link', !!(t.closest && t.closest('a, button, summary')) );
    }, { passive: true });
    addEventListener('pointerleave', function () { mx = my = -100; });
  }

  // magnetic buttons
  if (fine) document.querySelectorAll('.btn').forEach(function (b) {
    b.classList.add('mag');
    b.addEventListener('pointermove', function (e) {
      var r = b.getBoundingClientRect();
      b.style.transform = 'translate(' + ((e.clientX - r.left - r.width / 2) * .25).toFixed(1) + 'px,' + ((e.clientY - r.top - r.height / 2) * .35).toFixed(1) + 'px)';
    });
    b.addEventListener('pointerleave', function () { b.style.transform = ''; });
  });

  var bar = document.querySelector('.progress i');
  var pin = document.querySelector('.road-pin');
  var road = pin && pin.querySelector('.road');
  var wide = matchMedia('(min-width: 900px)');
  var sideBySide = matchMedia('(min-width: 1081px)');
  var lastY = scrollY, vel = 0;

  function layoutPin() {
    if (!pin) return;
    if (!wide.matches) { pin.classList.remove('pinned'); pin.style.height = ''; road.style.transform = ''; return; }
    pin.classList.add('pinned');
    pin.style.height = (innerHeight + Math.max(road.scrollWidth - innerWidth, 0)) + 'px';
  }

  function frame(now) {
    if (lenis) lenis.raf(now);
    var y = scrollY, h = root.scrollHeight - innerHeight;
    vel = vel * .8 + (y - lastY) * .2; lastY = y;
    if (bar) bar.style.setProperty('--p', h > 0 ? y / h : 0);

    if (collage && hero) {
      var hr = hero.getBoundingClientRect();
      if (hr.bottom > 0) {
        // side by side: 0 at the top, 1 after ~300px. stacked (phones): runs while the collage crosses the screen
        var cr = collage.getBoundingClientRect();
        var p = sideBySide.matches ? clamp(y / 300, 0, 1) : clamp((innerHeight * .45 - cr.top) / (cr.height * .6), 0, 1);
        p = p * p * (3 - 2 * p);
        collage.style.setProperty('--made', clamp((p - .7) / .3, 0, 1).toFixed(3));
        var tg = p > 0 ? markTargets() : null, W = collage.offsetWidth;
        pieces.forEach(function (o) {
          if (!o.held) {
            o.x += o.vx; o.y += o.vy; o.vx *= .92; o.vy *= .92;
            if (Math.abs(o.vx) < .05) o.vx = 0;
            if (Math.abs(o.vy) < .05) o.vy = 0;
          }
          var el = o.el, par = -y * o.depth * .12 * (1 - p);
          var tx = o.x * (1 - p), ty = o.y * (1 - p) + par, s = 1, rot = (o.vx * .6) * (1 - p), op = 1;
          if (tg && o.role) {
            var t = tg[o.role], w0 = el.offsetWidth, h0 = el.offsetHeight;
            s = 1 + (t.w / w0 - 1) * p;
            tx += ((t.x + t.w / 2) - (el.offsetLeft + w0 / 2)) * p;
            ty += ((t.y + t.h / 2) - (el.offsetTop + h0 / 2)) * p;
            rot -= o.r * p;
          } else if (p > 0) {
            var cxp = el.offsetLeft + el.offsetWidth / 2 - W / 2, cyp = el.offsetTop + el.offsetHeight / 2 - W / 2;
            tx += cxp * .9 * p; ty += cyp * .9 * p; op = 1 - p;
          }
          el.style.transform = 'translate3d(' + tx.toFixed(1) + 'px,' + ty.toFixed(1) + 'px,0) rotate(' + rot.toFixed(2) + 'deg) scale(' + s.toFixed(3) + ')';
          el.style.opacity = op.toFixed(3);
        });
      }
    }

    var rate = Math.min(6, 1 + Math.abs(vel) * .25);
    tracks.forEach(function (a) { a.playbackRate = (vel < -0.5 ? -1 : 1) * rate; });

    if (pin && pin.classList.contains('pinned')) {
      var r = pin.getBoundingClientRect(), span = pin.offsetHeight - innerHeight;
      var t2 = clamp(-r.top / (span || 1), 0, 1);
      road.style.transform = 'translate3d(' + (-t2 * Math.max(0, road.scrollWidth - innerWidth)).toFixed(1) + 'px,0,0)';
    }

    if (chars.length && fine && mx > 0) {
      var hb = hero.getBoundingClientRect();
      if (hb.bottom > 0) chars.forEach(function (c) {
        var r = c.getBoundingClientRect(), d = Math.hypot(r.left + r.width / 2 - mx, r.top + r.height / 2 - my);
        var k = clamp(1 - d / 220, 0, 1);
        c.style.setProperty('--wg', (700 - 300 * k).toFixed(0));
        c.style.setProperty('--ly', (-10 * k).toFixed(1) + 'px');
      });
    }

    if (cur && fine) {
      cx += (mx - cx) * .25; cy += (my - cy) * .25;
      cur.style.transform = 'translate3d(' + cx.toFixed(1) + 'px,' + cy.toFixed(1) + 'px,0)';
    }
    requestAnimationFrame(frame);
  }

  addEventListener('resize', layoutPin);
  layoutPin();
  requestAnimationFrame(frame);
})();
