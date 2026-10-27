// SPDX-License-Identifier: AGPL-3.0-only
import {createServer} from 'node:http';
import {readFile,writeFile} from 'node:fs/promises';
import {resolve,sep,extname} from 'node:path';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = resolve(process.argv[2]), services = process.argv.includes('--services');
// Serve beneath a project prefix, as GitHub Pages does, without adding any
// isolation headers. The production service worker must provide those.
const prefix = '/mux/';
const server = createServer(async (request,response) => {
  const name = new URL(request.url,'http://localhost').pathname;
  if (!name.startsWith(prefix)) {response.writeHead(404).end();return;}
  if (services) {
    if (name === prefix+'echo') {
      if (request.headers.authorization !== 'Bearer fixture') {response.writeHead(401).end();return;}
      const chunks = [];for await (const chunk of request) chunks.push(chunk);
      response.writeHead(200,{'Retry-After':'2','Location':'/next'}).end(Buffer.concat(chunks));return;
    }
    if (name === prefix+'slow') {
      const timer = setTimeout(() => response.end('late'),3000);
      request.on('close',() => clearTimeout(timer));return;
    }
    if (name === prefix+'large') {response.end(Buffer.alloc(65536,65));return;}
  }
  const relative = name.slice(prefix.length) || 'index.html';
  const file = resolve(root,relative);
  if (!file.startsWith(root+sep)) {response.writeHead(403).end();return;}
  try {
    response.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.json':'application/json','.wasm':'application/wasm','.ttf':'font/ttf'})[extname(file)] || 'application/octet-stream');
    response.end(await readFile(file));
  } catch {response.writeHead(404).end();}
});
await new Promise(resolve => server.listen(0,'127.0.0.1',resolve));
const browser = await chromium.launch({headless:true});
try {
  const page = await browser.newPage({viewport:{width:1200,height:800}});
  const errors = [];
  page.on('pageerror',error => errors.push(error.message));
  const url = `http://127.0.0.1:${server.address().port}${prefix}`;
  await page.goto(url+(services ? '':'?demo'),{waitUntil:'domcontentloaded'});
  if (services) {
    for (const round of [1,2]) {
      if (round === 2) await page.reload({waitUntil:'domcontentloaded'});
      await page.waitForFunction(() => document.body.dataset.servicesRound || document.body.dataset.servicesError,{},{timeout:60000});
      assert.equal(await page.evaluate(() => document.body.dataset.servicesError),undefined);
      assert.equal(await page.evaluate(() => document.body.dataset.servicesRound),String(round));
      assert.equal(await page.evaluate(() => crossOriginIsolated),true);
    }
  } else {
    await page.waitForFunction(() => crossOriginIsolated && document.querySelectorAll('[data-skiff-h]').length > 20,{},{timeout:120000});
    await page.getByText('Mad Tea Party',{exact:true}).first().click();
    const composer = page.locator('textarea:visible').first();
    await composer.waitFor({timeout:30000});
    await composer.fill('🙂draft ✓');
    await composer.evaluate(element => element.setSelectionRange(2,2));
    await composer.press('Shift+Enter');
    await page.waitForFunction(() => [...document.querySelectorAll('textarea')].some(element => element.value === '🙂\ndraft ✓'),{},{timeout:10000});
    await page.waitForFunction(() => [...document.querySelectorAll('textarea')].some(element => element.value === '🙂\ndraft ✓' && element.selectionStart === 3),{},{timeout:10000});
    await composer.fill('Browser draft ✓');
    await composer.press('Enter');
    await page.getByText('Browser draft ✓',{exact:true}).first().waitFor({timeout:30000});
    assert.equal(await composer.inputValue(),'');
    await page.screenshot({path:resolve(root,'../web-smoke.png')});
  }
  assert.deepEqual(errors,[]);
} catch (error) {
  await writeFile(resolve(root,'../web-smoke-error.txt'),String(error));
  throw error;
} finally {await browser.close();server.close();}
