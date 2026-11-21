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
