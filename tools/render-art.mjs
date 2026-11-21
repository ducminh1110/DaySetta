import { chromium } from 'playwright-core';
import { readFileSync } from 'fs';

const exe = '/home/ubuntu/.cache/ms-playwright/chromium-1243/chrome-linux-arm64/chrome';
const files = process.argv.slice(2);
const browser = await chromium.launch({ executablePath: exe, args: ['--no-sandbox'] });
for (const f of files) {
  const svg = readFileSync(f, 'utf8');
  const m = svg.match(/viewBox="0 0 ([\d.]+) ([\d.]+)"/);
  const w = Math.round(+m[1]), h = Math.round(+m[2]);
  const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
  await page.goto('file://' + f);
  await page.waitForTimeout(400);
  const out = '/home/ubuntu/daysetta-web/shots/art/' + f.split('/').pop().replace('.svg', '') + '.png';
  await page.screenshot({ path: out });
  console.log(out);
  await page.close();
}
await browser.close();
