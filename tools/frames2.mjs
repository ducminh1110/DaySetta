import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 1280, height: 800 } })).newPage();
await p.goto('file:///home/ubuntu/daysetta-web/index.html');
let k = 0; const shot = () => p.screenshot({ path: `shots/v3/fr2/f${String(k++).padStart(4, '0')}.jpg`, type: 'jpeg', quality: 70 });
for (let i = 0; i < 30; i++) { await shot(); await p.waitForTimeout(40); }
const pc = await p.locator('.collage .pc').nth(5).boundingBox();
await p.mouse.move(pc.x + 30, pc.y + 30); await p.mouse.down();
for (let i = 0; i < 14; i++) { await p.mouse.move(pc.x + 30 - i * 22, pc.y + 30 + i * 14); await shot(); }
await p.mouse.up(); for (let i = 0; i < 10; i++) await shot();
for (let y = 0; y <= 2600; y += 45) { await p.evaluate(v => scrollTo(0, v), y); await p.waitForTimeout(20); await shot(); }
const ny = await p.evaluate(() => document.getElementById('news').getBoundingClientRect().top + scrollY - 80);
await p.evaluate(v => scrollTo(0, v), ny); for (let i = 0; i < 40; i++) { await p.waitForTimeout(60); await shot(); }
await b.close(); console.log(k);
