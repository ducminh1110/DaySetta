import { chromium } from 'playwright-core';
const b = await chromium.launch({ executablePath: '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome', args: ['--no-sandbox'] });
const p = await (await b.newContext({ viewport: { width: 390, height: 844 }, reducedMotion: 'reduce' })).newPage();
await p.goto('file:///home/ubuntu/daysetta-web/index.html'); await p.waitForTimeout(500);
console.log(await p.evaluate(() => {
  const W = document.documentElement.clientWidth, out = [];
  document.querySelectorAll('body *').forEach(e => { const r = e.getBoundingClientRect(); if (r.right > W + 1 && !e.closest('.band')) out.push(e.tagName + '.' + e.className + ' ' + Math.round(r.right)); });
  return [document.documentElement.scrollWidth, W, out.slice(0, 12)];
}));
await b.close();
