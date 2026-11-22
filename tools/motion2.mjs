import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 1440, height: 900 } })).newPage();
const errs = []; p.on('pageerror', e => errs.push(e.message)); p.on('console', m => m.type() === 'error' && errs.push(m.text()));
await p.goto('file:///home/ubuntu/daysetta-web/index.html');
await p.waitForTimeout(400); await p.screenshot({ path: 'shots/v3/w-intro.png' });
await p.waitForTimeout(1600); await p.screenshot({ path: 'shots/v3/w-0.png' });
// drag the pink dot across
const box = await p.locator('.collage .pc').nth(5).boundingBox();
await p.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await p.mouse.down();
await p.mouse.move(box.x - 260, box.y + 180, { steps: 12 }); await p.mouse.up(); await p.waitForTimeout(600);
await p.screenshot({ path: 'shots/v3/w-drag.png' });
for (const y of [250, 450, 700]) { await p.evaluate(v => scrollTo(0, v), y); await p.waitForTimeout(500); await p.screenshot({ path: `shots/v3/w-s${y}.png` }); }
const ny = await p.evaluate(() => document.getElementById('news').getBoundingClientRect().top + scrollY - 80);
await p.evaluate(v => scrollTo(0, v), ny); await p.waitForTimeout(4000); await p.screenshot({ path: 'shots/v3/w-news.png' });
await p.evaluate(() => scrollTo(0, document.documentElement.scrollHeight)); await p.waitForTimeout(1500); await p.screenshot({ path: 'shots/v3/w-foot.png' });
await p.goto('file:///home/ubuntu/daysetta-web/devlog/index.html'); await p.waitForTimeout(800);
await p.click('.f-btn[data-f="kernel"]'); await p.waitForTimeout(300); await p.screenshot({ path: 'shots/v3/w-devlog.png' });
console.log('errors:', errs.length ? errs : 'none');
await b.close();
