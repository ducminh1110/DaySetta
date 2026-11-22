import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true })).newPage();
const errs = []; p.on('pageerror', e => errs.push(e.message));
await p.goto('file:///home/ubuntu/daysetta-web/index.html'); await p.waitForTimeout(2000);
for (const y of [0, 500, 800, 1100]) { await p.evaluate(v => scrollTo(0, v), y); await p.waitForTimeout(600); await p.screenshot({ path: `shots/v3/mb-${y}.png` }); }
console.log('errors', errs.length ? errs : 'none'); await b.close();
