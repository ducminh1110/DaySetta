// theme toggle only — everything else is plain html
(function () {
  var btn = document.getElementById('themeBtn');
  var root = document.documentElement;
  function sync() {
    btn.setAttribute('aria-pressed', root.dataset.theme === 'dark' ? 'true' : 'false');
  }
  sync();
  btn.addEventListener('click', function () {
    var dark = root.dataset.theme === 'dark';
    if (dark) delete root.dataset.theme; else root.dataset.theme = 'dark';
    try { localStorage.setItem('ds-theme', dark ? 'light' : 'dark'); } catch (e) {}
    sync();
  });
})();