import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
for (const [w, h] of [[1920, 940], [1440, 760], [1366, 700], [1280, 650], [2560, 1300]]) {
  const p = await (await b.newContext({ viewport: { width: w, height: h } })).newPage();
  await p.goto('file:///home/ubuntu/daysetta-web/index.html'); await p.waitForTimeout(1200);
  const y = await p.evaluate(() => document.getElementById('roadmap').getBoundingClientRect().top + scrollY + 200);
  await p.evaluate(v => scrollTo(0, v), y); await p.waitForTimeout(900);
  const r = await p.evaluate(() => { const v = document.querySelector('.road-view').getBoundingClientRect(), s = document.querySelector('.step').getBoundingClientRect(), pinned = document.querySelector('.road-pin').classList.contains('pinned'); return { pinned, viewH: Math.round(v.height), stepH: Math.round(s.height), stepBottom: Math.round(s.bottom), inner: innerHeight }; });
  console.log(w + 'x' + h, JSON.stringify(r), r.viewH + 2 >= r.stepH && r.stepBottom <= r.inner ? 'OK' : 'CUT');
  await p.screenshot({ path: `shots/v3/road-${w}x${h}.png` }); await p.close();
}
await b.close();
