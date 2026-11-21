import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 1280, height: 800 } })).newPage();
await p.goto('file:///home/ubuntu/daysetta-web/index.html');
let k = 0; const shot = () => p.screenshot({ path: `shots/v3/fr/f${String(k++).padStart(4, '0')}.jpg`, type: 'jpeg', quality: 70 });
for (let i = 0; i < 40; i++) { await shot(); await p.waitForTimeout(30); }
const H = await p.evaluate(() => document.documentElement.scrollHeight - innerHeight);
for (let y = 0; y <= H; y += 40) { await p.evaluate(v => scrollTo(0, v), y); await p.waitForTimeout(25); await shot(); }
for (let i = 0; i < 20; i++) await shot();
await b.close(); console.log(k, 'frames');
