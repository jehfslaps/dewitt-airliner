// node render.js  ->  "How to Set Up MIDI/<nn> <DAW>.png", 1080x1350 each (run python3 gen.py first)
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const out = path.join(__dirname, 'How to Set Up MIDI');
  fs.mkdirSync(out, { recursive: true });
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1080, height: 1350 }, deviceScaleFactor: 1 });
  for (const f of fs.readdirSync(path.join(__dirname, 'html')).filter(f => f.endsWith('.html')).sort()) {
    await p.goto('file://' + path.join(__dirname, 'html', f));
    await p.evaluate(() => document.fonts.ready);
    const over = await p.evaluate(() => {   // nothing may run into the footer or off the page
      const steps = document.querySelector('.note').getBoundingClientRect().bottom;
      return { bottom: Math.round(steps), foot: Math.round(document.querySelector('.foot').getBoundingClientRect().top),
               wide: document.documentElement.scrollWidth > 1080 };
    });
    await p.screenshot({ path: path.join(out, f.replace('.html', '.png')) });
    console.log(f, over.bottom > over.foot - 16 || over.wide ? `TOO TALL/WIDE (${over.bottom} vs ${over.foot})` : `ok (${over.foot - over.bottom}px spare)`);
  }
  await b.close();
})();
