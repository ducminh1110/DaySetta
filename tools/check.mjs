import { chromium } from 'playwright-core';
const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const browser = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
const page = await ctx.newPage();
const errors = [];
page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', e => errors.push(String(e)));

await page.goto('file:///home/ubuntu/daysetta-web/index.html');
// theme toggle round trip
await page.click('#themeBtn');
const dark = await page.evaluate(() => document.documentElement.dataset.theme);
await page.click('#themeBtn');
const light = await page.evaluate(() => document.documentElement.dataset.theme || 'light');
console.log('toggle:', dark, '->', light);
// persistence
await page.click('#themeBtn');
await page.reload();
console.log('persisted after reload:', await page.evaluate(() => document.documentElement.dataset.theme));
// faq accordion
await page.click('.faq-list summary');
console.log('faq open:', await page.evaluate(() => document.querySelector('.faq-list details').open));
// anchors exist
for (const id of ['what', 'how', 'status', 'roadmap', 'faq', 'devlog']) {
  const n = await page.locator('#' + id).count();
  if (n !== 1) console.log('MISSING anchor', id);
}
// devlog anchors from teaser cards
await page.goto('file:///home/ubuntu/daysetta-web/devlog/index.html');
for (const id of ['gpu6', 'gpu4', 'postlogin']) {
  const n = await page.locator('#' + id).count();
  if (n !== 1) console.log('MISSING devlog anchor', id);
}
// all images loaded
const broken = await page.evaluate(() => [...document.images].filter(i => !i.complete || !i.naturalWidth).map(i => i.src));
console.log('broken images:', broken.length ? broken : 'none');
await page.goto('file:///home/ubuntu/daysetta-web/index.html');
await page.evaluate(async () => { for (let y = 0; y < document.body.scrollHeight; y += 700) { scrollTo(0, y); await new Promise(r => setTimeout(r, 40)); } });
await page.waitForTimeout(400);
const broken2 = await page.evaluate(() => [...document.images].filter(i => !i.complete || !i.naturalWidth).map(i => i.src));
console.log('broken images home:', broken2.length ? broken2 : 'none');
console.log('console errors:', errors.length ? errors : 'none');
await browser.close();