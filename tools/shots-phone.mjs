import { chromium } from 'playwright-core';
const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const browser = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
const ctx = await browser.newContext({ viewport: { width: 360, height: 780 }, reducedMotion: 'reduce' });
const page = await ctx.newPage();
await page.goto('file:///home/ubuntu/daysetta-web/index.html');
await page.evaluate(async () => {
  for (let y = 0; y < document.body.scrollHeight; y += 600) { scrollTo(0, y); await new Promise(r => setTimeout(r, 50)); }
});
for (const [id, name] of [['how', 'how'], ['status', 'status'], ['faq', 'faq'], ['roadmap', 'roadmap']]) {
  await page.locator('#' + id).scrollIntoViewIfNeeded();
  await page.waitForTimeout(300);
  await page.screenshot({ path: `/home/ubuntu/daysetta-web/shots/home-360-${name}.png` });
}
// horizontal overflow check
const overflow = await page.evaluate(() => document.documentElement.scrollWidth - document.documentElement.clientWidth);
console.log('overflow px:', overflow);
await browser.close();