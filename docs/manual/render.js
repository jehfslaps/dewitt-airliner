// node render.js -> Dewitt_User_Manual.pdf (run python3 gen.py first). Reports any page whose text runs over.
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const b = await chromium.launch(); const p = await b.newPage();
  await p.goto('file://' + path.join(__dirname, 'manual.html'), { waitUntil: 'networkidle' });
  await p.evaluate(() => document.fonts.ready);
  const over = await p.evaluate(() => [...document.querySelectorAll('.page')].map((pg, i) => {
    const foot = pg.querySelector('.foot').getBoundingClientRect().top;
    let bottom = 0;
    for (const el of pg.children) if (!el.classList.contains('foot') && !el.classList.contains('rail') && !el.classList.contains('end') && !el.classList.contains('covergrid'))
      bottom = Math.max(bottom, el.getBoundingClientRect().bottom);
    return { page: i + 1, spare: Math.round(foot - bottom) };
  }));
  for (const o of over) console.log(`page ${o.page}: ${o.spare < 4 ? 'TOO LONG' : 'ok'} (${o.spare}px spare)`);
  await p.pdf({ path: path.join(__dirname, 'Dewitt_User_Manual.pdf'), format: 'A4', printBackground: true, preferCSSPageSize: true });
  await b.close();
})();
