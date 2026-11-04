// Requires Playwright and installed Chrome. All requests stay in local fixtures.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '..');
const out = path.join(root, '.pio', 'student-layout-check');
fs.mkdirSync(out, {recursive:true});

(async () => {
  const browser = await chromium.launch({channel:'chrome', headless:true});
  try {
    for (const width of [320, 390, 576, 1280]) {
      const page = await browser.newPage({viewport:{width, height:800}});
      let state = null;
      await page.addInitScript(() => {
        // Keep page sockets disconnected so the shared client polls the fixture.
        window.WebSocket = class extends EventTarget {
          static OPEN=1; static CONNECTING=0; static CLOSED=3;
          readyState=0; send() {} close() { this.readyState=3; }
        };
      });
      await page.route('**/*', async route => {
        const url = new URL(route.request().url());
        if (url.pathname === '/api/competition/state') {
          return route.fulfill({status:state ? 200 : 404, json:state || {}});
        }
        const relative = decodeURIComponent(url.pathname).replace(/^\//,'');
        const file = path.resolve(root, 'data', relative);
        if (!file.startsWith(path.join(root,'data') + path.sep)) return route.abort();
        const gz = file + '.gz';
        if (!fs.existsSync(file) && !fs.existsSync(gz)) return route.fulfill({status:404,body:''});
        const body = fs.existsSync(file) ? fs.readFileSync(file) : zlib.gunzipSync(fs.readFileSync(gz));
        const types={'.html':'text/html; charset=utf-8','.js':'text/javascript; charset=utf-8','.css':'text/css','.json':'application/json'};
        await route.fulfill({body,contentType:types[path.extname(file)] || 'application/octet-stream'});
      });
      for (const name of ['index','set','blockly','joy','ai','hardware_test','wiring','plotter','blockly_test']) {
        for (const mode of ['unsupported','teaching','managed']) {
          state = mode === 'unsupported' ? null : {
            type:'competition_state', boot_id:'test', generation:1, epoch:1,
            managed:mode === 'managed', state:mode === 'managed' ? 'PAUSED' : 'TEACHING',
            paired:true,
            state_fresh:true, allowed:false, channel:8, remaining_ms:30000,
            controller_mac:mode === 'managed' ? 'AA:BB:CC:DD:EE:FF' : '',
          };
          await page.goto(`http://car/${name}.html`);
          if (state) await page.locator('#competitionStatus').waitFor({state:'attached'});
          if (name === 'blockly') await page.locator('#loadingOverlay').waitFor({state:'hidden'});
          if (state && name === 'set') {
            assert.equal(await page.locator('#competitionRecovery').isVisible(),true);
            assert.equal(await page.locator('#competitionRecovery').evaluate(el=>el.open),false);
            await page.locator('#competitionRecovery summary').click();
            assert.equal(await page.locator('#competitionHandover').isVisible(),true);
            await page.locator('#competitionRecovery summary').click();
          }
          const layout = await page.evaluate(() => {
            const rect = el => { const r=el.getBoundingClientRect(); return {x:r.x,y:r.y,right:r.right,bottom:r.bottom,width:r.width,height:r.height}; };
            const banner=document.querySelector('#competitionStatus:not([hidden])');
            const content=document.querySelector('.container');
            return {viewport:innerWidth, scroll:document.documentElement.scrollWidth,
              banner:banner && rect(banner), content:content && rect(content),
              controls:banner ? [...banner.children].filter(e=>!e.hidden).map(rect) : [],
              editorHost:document.getElementById('competitionStatusHost')?.contains(banner),
              runButton:document.getElementById('runBtn') && rect(document.getElementById('runBtn')),
              appBottom:document.querySelector('.app')?.getBoundingClientRect().bottom};
          });
          const context=`${name} ${width}px ${mode}`;
          if (['index','set'].includes(name)) assert.ok(layout.scroll <= width + 1, `${context}: horizontal overflow ${JSON.stringify(layout)}`);
          if (state?.managed) {
            assert.ok(layout.banner.x >= -1 && layout.banner.right <= width + 1, `${context}: banner offscreen`);
            for (const control of layout.controls) assert.ok(control.x >= -1 && control.right <= width + 1, `${context}: banner child offscreen`);
            if (['index','set'].includes(name)) assert.ok(layout.content.y >= layout.banner.bottom, `${context}: content must be below banner`);
            if (name === 'blockly') {
              assert.equal(layout.editorHost,true,`${context}: banner outside editor shell`);
              assert.ok(layout.appBottom <= 801,`${context}: editor pushed below viewport`);
              assert.ok(layout.runButton.bottom <= 801,`${context}: run button below viewport`);
            }
          } else assert.equal(layout.banner,null,`${context}: no banner on teaching-only firmware`);
          if (width === 390 && ['index','set','blockly'].includes(name) && mode !== 'unsupported') {
            await page.screenshot({path:path.join(out,`${name}-${mode}.png`),fullPage:true});
          }
        }
      }
      await page.close();
      console.log(`PASS student pages at ${width}px: teaching-only, teaching and managed states`);
    }
  } finally { await browser.close(); }
})().catch(e => {console.error(e);process.exitCode=1;});
