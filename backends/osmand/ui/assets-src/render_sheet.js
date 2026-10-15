// render_sheet.js SHEET.html OUT.png WIDTH HEIGHT
// Screenshots an icon sheet with a transparent background in headless
// Chromium (playwright-core; CHROME=path to chrome or chrome-headless-shell).
const { chromium } = require('playwright-core');

(async () => {
  const [sheet, out, width, height] = process.argv.slice(2);
  const browser = await chromium.launch({ executablePath: process.env.CHROME, args: ['--no-sandbox'] });
  const page = await browser.newPage({
    viewport: { width: Number(width), height: Number(height) }, deviceScaleFactor: 1,
  });
  await page.goto('file://' + sheet);
  await page.screenshot({ path: out, omitBackground: true });
  await browser.close();
})();
