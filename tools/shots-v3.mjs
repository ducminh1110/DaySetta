import { chromium } from 'playwright-core';
const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const base = 'file:///home/ubuntu/daysetta-web/';
const out = '/home/ubuntu/daysetta-web/shots/v3/';
const browser = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
const jobs = process.argv.slice(2).length ? JSON.parse(process.argv[2]) : [
  ['index.html', 'home', 1440, 900, 'light'], ['index.html', 'home', 390, 844, 'light'],
  ['index.html', 'home', 1440, 900, 'dark'], ['devlog/index.html', 'devlog', 1440, 900, 'light']];
for (const [p, n, w, h, theme] of jobs) {
  const ctx = await browser.newContext({ viewport: { width: w, height: h }, colorScheme: theme, reducedMotion: 'reduce' });
  const page = await ctx.newPage();
  await page.goto(base + p); await page.waitForTimeout(500);
  await page.screenshot({ path: `${out}${n}-${w}-${theme}-full.png`, fullPage: true });
  await page.screenshot({ path: `${out}${n}-${w}-${theme}-hero.png` });
  await ctx.close();
}
await browser.close();
