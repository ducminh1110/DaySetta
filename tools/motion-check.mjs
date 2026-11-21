import { chromium } from 'playwright-core';
const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const b = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 1440, height: 900 } })).newPage();
const errs = []; p.on('pageerror', e => errs.push(e.message)); p.on('console', m => m.type() === 'error' && errs.push(m.text()));
await p.goto('file:///home/ubuntu/daysetta-web/index.html'); await p.waitForTimeout(1800);
await p.screenshot({ path: 'shots/v3/m-0.png' });
for (const id of ['how', 'roadmap', 'faq', 'devlog']) {
  const y = await p.evaluate(i => document.getElementById(i).getBoundingClientRect().top + scrollY, id);
  for (const dy of id === 'roadmap' ? [0, 700, 1400] : id === 'how' ? [300, 900] : [100]) {
    await p.evaluate(v => scrollTo(0, v), y + dy); await p.waitForTimeout(1200);
    await p.screenshot({ path: `shots/v3/m-${id}-${dy}.png` });
  }
}
console.log('errors:', errs.length ? errs : 'none');
await b.close();
