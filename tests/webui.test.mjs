import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, join, extname } from 'node:path';
import { chromium } from 'playwright';
const root = resolve(import.meta.dirname, '..');
const temp = mkdtempSync(join(tmpdir(), 'lh-webui-'));
mkdirSync(join(temp, 'runtime'));
writeFileSync(join(temp, 'runtime/system_packages.txt'), 'android\ncom.android.settings\n');
const cpp = join(root, 'module/src/main/cpp');
const ctl = join(temp, 'configctl');
execFileSync(process.env.CXX || 'c++', ['-std=c++20', '-DLH_HOST_TEST', `-DLH_STATE_DIR="${temp}/state"`,
  `-DLH_RUNTIME_DIR="${temp}/runtime"`, ...['config.cpp', 'configctl.cpp', 'susfs_backend.cpp', 'path_keeper.cpp'].map(name => join(cpp, name)), '-o', ctl]);
execFileSync(ctl, ['init', 'lineage']);
const server = createServer((req, res) => {
  const path = join(root, 'module/webroot', req.url === '/' ? 'index.html' : req.url);
  if (!path.startsWith(join(root, 'module/webroot') + '/')) { res.writeHead(403).end(); return; }
  try {
    res.setHeader('Content-Type', {'.html':'text/html', '.js':'text/javascript', '.css':'text/css'}[extname(path)] || 'text/plain');
    res.end(readFileSync(path));
  } catch { res.writeHead(404).end(); }
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const browser = await chromium.launch({headless: true, ...(process.env.CHROMIUM_PATH ? {executablePath: process.env.CHROMIUM_PATH} : {})});
const page = await browser.newPage({viewport: {width:390, height:844}, deviceScaleFactor:1});
const errors = []; page.on('pageerror', error => errors.push(error.message));
const commands = [];
await page.exposeFunction('nativeExec', command => {
  commands.push(command);
  const prefix = '/data/adb/modules/lineage_hide/bin/configctl ';
  if (command.startsWith(prefix)) {
    const p = spawnSync('/bin/sh', ['-c', `'${ctl}' ` + command.slice(prefix.length)], {encoding:'utf8'});
    return {errno:p.status, stdout:p.stdout, stderr:p.stderr};
  }
  assert.ok(command.startsWith("/system/bin/toybox timeout -s KILL 10 /system/bin/sh -c '/system/bin/cmd package list packages -3 --user "));
  return {errno:0, stdout:'package:org.example.alpha\npackage:org.example.beta\n', stderr:''};
});
await page.addInitScript(() => { window.ksu = {exec(command, options, callback) {
  window.nativeExec(command).then(r => window[callback](r.errno, r.stdout, r.stderr));
}}; });
try {
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  await page.waitForFunction(() => !document.querySelector('#fields').disabled);
  assert.equal(await page.locator('#enabled').isChecked(), true);
  assert.equal(await page.locator('#native_paths').isChecked(), true);
  assert.equal(await page.locator('#legacy_boot').isChecked(), false);
  assert.match(await page.locator('#path_keyword').inputValue(), /lineage\ngapps/);
  assert.match(await page.locator('#runtime').textContent(), /清单已就绪/);
  assert.match(await page.locator('#paths-effect').textContent(), /未验证/);
  // A saved receipt must never masquerade as current maintenance or app proof.
  writeFileSync(join(temp, 'runtime/paths.status'), 'paths_status=applied\npaths_count=64\nmaps_count=3\n');
  await page.click('#reload');
  await page.waitForFunction(() => document.querySelector('#paths-lifetime').textContent.includes('未建立引用保活'));
  assert.match(await page.locator('#paths-status').textContent(), /最近登记回执.*64 个路径 \/ 3 个映射/);
  assert.match(await page.locator('#paths-effect').textContent(), /未验证/);
  writeFileSync(join(temp, 'runtime/paths.status'), 'paths_status=applied\npaths_count=64\nmaps_count=3\npaths_lifetime_mode=inode_refs\n');
  await page.click('#reload');
  await page.waitForFunction(() => document.querySelector('#paths-lifetime').textContent.includes('无法确认保活'));
  assert.match(await page.locator('#paths-effect').textContent(), /未验证/);
  rmSync(join(temp, 'runtime/paths.status'));
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  await page.selectOption('#mode', 'include');
  await page.click('#load-apps');
  await page.waitForSelector('.app-row');
  await page.locator('.app-row').filter({hasText:'org.example.alpha'}).locator('input').check();
  await page.selectOption('#preset', 'evox'); await page.click('#load-preset');
  await page.waitForFunction(() => document.querySelector('#service_keyword').value.includes('evolution'));
  assert.match(await page.locator('#path_keyword').inputValue(), /evolution/);
  assert.match(await page.locator('#hide_path').inputValue(), /\/system\/lib64\/libstagefright\.so/);
  assert.equal(await page.locator('#mode').inputValue(), 'include');
  assert.equal(await page.locator('#package').inputValue(), 'org.example.alpha');
  await page.click('#save'); await page.waitForFunction(() => document.querySelector('#dirty').textContent === '配置已保存');
  let saved = execFileSync(ctl, ['get'], {encoding:'utf8'});
  assert.match(saved, /profile=evox/); assert.match(saved, /mode=include/); assert.match(saved, /package=org.example.alpha/);
  assert.match(saved, /native_paths=1/); assert.match(saved, /path_keyword=evolution/);
  // Presets must not re-enable globally scoped path handling after the user disables it.
  await page.uncheck('#native_paths');
  await page.click('#load-preset');
  await page.waitForFunction(() => !document.querySelector('#fields').disabled);
  assert.equal(await page.locator('#native_paths').isChecked(), false);
  await page.click('#reload');
  await page.waitForFunction(() => document.querySelector('#native_paths').checked);
  // The preview reads the current draft and must not save it or apply inode marks.
  await page.locator('details').filter({has:page.locator('#hide_path')}).locator('summary').click();
  const hidePaths = await page.locator('#hide_path').inputValue();
  await page.fill('#hide_path', hidePaths + '\n/product/overlay/example/evolution_test.apk');
  await page.click('#preview-paths'); await page.waitForSelector('#path-plan');
  assert.match(await page.locator('#message').textContent(), /只读预览/);
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  assert.equal(commands.some(command => command.includes("'apply-paths'")), false);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  // Traversal is rejected before invoking the preview backend, preserving stored rules.
  await page.fill('#hide_path', '/system/../data/local/tmp/injected');
  const planCalls = commands.filter(command => command.includes("'path-plan'")).length;
  await page.click('#preview-paths'); await page.waitForSelector('#message.error');
  assert.equal(commands.filter(command => command.includes("'path-plan'")).length, planCalls);
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  await page.click('#reload');
  await page.waitForFunction(() => document.querySelector('#hide_path').value.includes('libstagefright.so') &&
    !document.querySelector('#hide_path').value.includes('injected'));
  // Text export remains usable in managers with no WebView DownloadListener.
  await page.click('#export'); await page.waitForSelector('#export-panel');
  assert.equal(await page.locator('#export-text').inputValue(), saved);
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  await page.click('#copy-export');
  await page.waitForFunction(() => document.querySelector('#message').textContent.includes('已复制') ||
    document.querySelector('#message').textContent.includes('已选中'));
  const [download] = await Promise.all([page.waitForEvent('download'), page.click('#download-export')]);
  assert.equal(download.suggestedFilename(), 'lineage-hide.conf');
  assert.equal(readFileSync(await download.path(), 'utf8'), saved);
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
  // Native validation must reject malicious input without shell interpolation or overwriting the saved file.
  const sentinel = join(temp, 'injected');
  await page.fill('#package', `org.example.alpha'\n$(touch ${sentinel})`);
  await page.click('#save'); await page.waitForSelector('#message.error');
  assert.equal(existsSync(sentinel), false);
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  await page.click('#reload'); await page.waitForFunction(() => document.querySelector('#package').value === 'org.example.alpha');
  // Import validates before changing the draft, and never saves automatically.
  const imported = saved.replace('mode=include', 'mode=exclude');
  await page.setInputFiles('#file', {name:'import.conf', mimeType:'text/plain', buffer:Buffer.from(imported)});
  await page.waitForFunction(() => document.querySelector('#mode').value === 'exclude');
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  await page.click('#save'); await page.waitForFunction(() => document.querySelector('#dirty').textContent === '配置已保存');
  saved = execFileSync(ctl, ['get'], {encoding:'utf8'}); assert.match(saved, /mode=exclude/);
  await page.setInputFiles('#file', {name:'bad.conf', mimeType:'text/plain', buffer:Buffer.from('schema=999\n')});
  await page.waitForSelector('#message.error');
  assert.equal(execFileSync(ctl, ['get'], {encoding:'utf8'}), saved);
  assert.equal(await page.locator('#mode').inputValue(), 'exclude');
  assert.equal(await page.locator('#package').inputValue(), 'org.example.alpha');
  await page.reload(); await page.waitForFunction(() => !document.querySelector('#fields').disabled);
  await page.evaluate(() => scrollTo(0,0));
  mkdirSync(join(root, 'out'), {recursive:true});
  await page.screenshot({path:join(root, 'out/webui-mobile.png'), fullPage:true});
  await page.emulateMedia({colorScheme:'dark'});
  await page.screenshot({path:join(root, 'out/webui-dark.png'), fullPage:true});
  assert.deepEqual(errors, []);
  // No bridge: explicit error, no pretend success.
  const plain = await browser.newPage();
  await plain.goto(`http://127.0.0.1:${server.address().port}`);
  await plain.waitForSelector('#message.error');
  assert.equal(await plain.locator('#save').isDisabled(), true);
  assert.match(await plain.locator('#message').textContent(), /KernelSU/);
  console.log(`WebUI integration passed (${commands.length} bridge calls; real host configctl, mocked app list).`);
} finally {
  await browser.close(); server.close(); rmSync(temp, {recursive:true,force:true});
}
