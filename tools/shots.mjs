import { chromium } from 'playwright-core';

const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const base = 'file:///home/ubuntu/daysetta-web/';
const shots = '/home/ubuntu/daysetta-web/shots/';
const browser = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });

const jobs = [
  ['index.html', 'home', 1440, 900, 'light'],
  ['index.html', 'home', 360, 780, 'light'],
  ['index.html', 'home', 1440, 900, 'dark'],
  ['index.html', 'home', 360, 780, 'dark'],
  ['devlog/index.html', 'devlog', 1440, 900, 'light'],
  ['devlog/index.html', 'devlog', 360, 780, 'light'],
  ['devlog/index.html', 'devlog', 1440, 900, 'dark'],
];

for (const [page_, name, w, h, theme] of jobs) {
  const ctx = await browser.newContext({
    viewport: { width: w, height: h },
    colorScheme: theme,
    reducedMotion: 'reduce',
  });
  const page = await ctx.newPage();
  await page.goto(base + page_);
  await page.waitForTimeout(400);
  // walk the page so lazy images load before the full-page shot
  await page.evaluate(async () => {
    for (let y = 0; y < document.body.scrollHeight; y += 600) {
      scrollTo(0, y);
      await new Promise(r => setTimeout(r, 60));
    }
    scrollTo(0, 0);
  });
  await page.waitForTimeout(500);
  const suffix = `${name}-${w}-${theme}`;
  await page.screenshot({ path: shots + suffix + '-full.png', fullPage: true });
  await page.screenshot({ path: shots + suffix + '-hero.png' });
  console.log(suffix);
  await ctx.close();
}
await browser.close();