import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const errs = [];
for (const [path, name, w, h] of [['index.html', 'home', 1440, 900], ['docs/index.html', 'docs', 1440, 900], ['docs/how-it-works/index.html', 'doc', 1440, 900],
  ['blog/index.html', 'blog', 1440, 900], ['blog/chasing-a-black-screen/index.html', 'post', 1440, 900], ['docs/how-it-works/index.html', 'doc-m', 390, 844], ['blog/chasing-a-black-screen/index.html', 'post-m', 390, 844]]) {
  const p = await (await b.newContext({ viewport: { width: w, height: h } })).newPage();
  p.on('pageerror', e => errs.push(name + ': ' + e.message)); p.on('console', m => m.type() === 'error' && errs.push(name + ': ' + m.text()));
  await p.goto('file:///home/ubuntu/daysetta-web/' + path); await p.waitForTimeout(1800);
  if (name === 'home') { await p.mouse.move(400, 330); await p.waitForTimeout(700); }
  if (name === 'doc' || name === 'post') { await p.mouse.wheel(0, 700); await p.waitForTimeout(1500); }
  await p.screenshot({ path: `shots/v3/pg-${name}.png` });
  const sw = await p.evaluate(() => [document.documentElement.scrollWidth, innerWidth]);
  if (sw[0] > sw[1]) errs.push(name + ' overflow ' + sw);
  await p.close();
}
console.log(errs.length ? errs : 'no errors'); await b.close();
