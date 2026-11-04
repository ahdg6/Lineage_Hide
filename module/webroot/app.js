import { control, exec } from './bridge.js';
import { flags, lists, parse, serialize, lines } from './config.js';
const $ = id => document.getElementById(id);
let config, saved = '', installed = [], busy = false;
function message(text, error = false) { $('message').textContent = text; $('message').className = error ? 'error' : ''; }
function read() {
  const c = { profile: config.profile, mode: $('mode').value };
  for (const key of flags) c[key] = $(key).checked;
  for (const key of lists) c[key] = lines($(key).value);
  return c;
}
function paint(c) {
  config = c;
  for (const key of flags) $(key).checked = c[key];
  for (const key of lists) $(key).value = c[key].join('\n');
  $('mode').value = c.mode;
  if (c.profile !== 'custom') $('preset').value = c.profile;
  changed(); renderApps();
}
function changed() {
  if (!config) return;
  $('path-plan').hidden = true;
  $('export-panel').hidden = true;
  const c = read();
  $('dirty').textContent = serialize(c) === saved ? '配置已保存' : '有未保存的修改';
  $('profile-label').textContent = {lineage: 'LineageOS 规则', evox: 'Evolution X 规则', custom: '自定义规则'}[c.profile];
  $('selection-count').textContent = `${c.package.length} 个包名`;
  $('scope-hint').textContent = {all: '当前覆盖全部三方应用，下面的名单暂不参与匹配。', include: '仅对名单中的三方应用启用隐藏。', exclude: '对全部三方应用启用隐藏，名单中的应用除外。'}[c.mode];
}
async function run(task) {
  if (busy) return;
  busy = true; $('fields').disabled = true;
  try { await task(); } catch (error) { message(error.message, true); }
  finally { busy = false; $('fields').disabled = !config; }
}
async function status() {
  try {
    const text = await control('status');
    $('runtime').textContent = text.includes('system_list_ready=1') ? '系统应用清单已就绪 · 等待应用验证' : '系统应用清单未就绪 · 暂停注入';
    $('legacy-status').textContent = '本次启动：' + (text.match(/^legacy_boot=(.*)$/m)?.[1] ?? '未知');
    const pathState = text.match(/^paths_status=(.*)$/m)?.[1] ?? 'not_started';
    const states = {not_started: '尚无登记回执', disabled: '未执行登记（已关闭）', applied: '登记命令已完成',
      partial: '部分登记成功，请检查 paths.log',
      failed: '登记失败，请检查 paths.log', unsupported: '未找到支持的 SUSFS 后端'};
    const counts = ['paths_count', 'maps_count'].map(key => text.match(new RegExp(`^${key}=(\\d+)$`, 'm'))?.[1]);
    $('paths-status').textContent = '最近登记回执：' + (states[pathState] ?? pathState) +
      (counts.every(value => value !== undefined) ? ` · ${counts[0]} 个路径 / ${counts[1]} 个映射` : '');
    const lifetime = text.match(/^paths_lifetime=(.*)$/m)?.[1] ?? 'unknown';
    const held = text.match(/^paths_pinned_count=(\d+)$/m)?.[1];
    const lifetimes = {held: '文件引用保活中', changed: '路径对象已变化，请重启重新登记',
      unavailable: '无法确认保活，旧回执不能代表当前效果', unmanaged: '只有一次性登记，未建立引用保活',
      inactive: '未启动或无需保活', unknown: '无法读取'};
    $('paths-lifetime').textContent = '生命周期：' + (lifetimes[lifetime] ?? lifetimes.unknown) +
      (held !== undefined ? ` · ${held} 个文件引用` : '');
    if (text.includes('paths_config_current=no\n')) $('paths-lifetime').textContent += ' · 路径配置有变更，重启后应用';
    if (text.includes('paths_config_current=unknown\n')) $('paths-lifetime').textContent += ' · 无法核对已保存配置';
    $('paths-effect').textContent = '应用侧效果：未验证。需分别检查目标应用首次读取与持续运行；登记数量、保活状态均不代表检测通过。';
  } catch {
    $('runtime').textContent = '无法读取启动状态'; $('paths-status').textContent = '无法读取登记回执';
    $('paths-lifetime').textContent = '生命周期：无法读取'; $('paths-effect').textContent = '应用侧效果：未验证';
  }
}
async function reload() {
  try {
    const text = await control('get');
    const c = parse(text); saved = serialize(c); paint(c);
    message('已读取设备配置。现有应用进程继续使用启动时的规则。');
  } catch (error) {
    if (!config && window.ksu) {
      paint(parse(await control('preset', 'lineage')));
      message(`配置读取失败：${error.message}。已载入 LineageOS 草稿，检查后保存可修复配置。`, true);
    } else throw error;
  }
  await status();
}
function renderApps() {
  const selected = new Set(lines($('package').value));
  const query = $('search').value.trim().toLowerCase();
  $('apps').replaceChildren();
  for (const pkg of installed.filter(p => p.toLowerCase().includes(query))) {
    const label = document.createElement('label'); label.className = 'app-row';
    const input = document.createElement('input'); input.type = 'checkbox'; input.checked = selected.has(pkg);
    input.addEventListener('change', () => {
      const names = new Set(lines($('package').value));
      input.checked ? names.add(pkg) : names.delete(pkg);
      $('package').value = [...names].sort().join('\n'); changed();
    });
    const name = document.createElement('span'); name.textContent = pkg;
    label.append(input, name); $('apps').append(label);
  }
  if (!$('apps').childElementCount) $('apps').textContent = '没有匹配的三方应用';
}
$('settings').addEventListener('input', event => {
  if (!config) return;
  if ((lists.includes(event.target.id) && event.target.id !== 'package') ||
      flags.filter(k => !['enabled', 'legacy_boot'].includes(k)).includes(event.target.id)) config.profile = 'custom';
  changed();
  if (event.target.id === 'package') renderApps();
});
$('search').addEventListener('input', renderApps);
$('reload').addEventListener('click', () => run(reload));
$('load-preset').addEventListener('click', () => run(async () => {
  const previous = read(), next = parse(await control('preset', $('preset').value));
  for (const key of ['enabled', 'mode', 'package', 'native_paths', 'legacy_boot']) next[key] = previous[key];
  paint(next); message('预设规则已载入草稿；应用名单和全局开关保留。点击保存后生效。');
}));
$('load-apps').addEventListener('click', () => run(async () => {
  const output = await exec('/system/bin/toybox timeout -s KILL 10 /system/bin/sh -c \'/system/bin/cmd package list packages -3 --user "$(/system/bin/am get-current-user)"\'');
  installed = [...new Set(output.split(/\r?\n/).filter(x => x.startsWith('package:')).map(x => x.slice(8)))]
    .filter(x => /^[a-zA-Z_][\w]*(\.[a-zA-Z_][\w]*)+$/.test(x)).sort();
  $('app-picker').hidden = false; renderApps();
  message(`已读取当前 Android 用户的 ${installed.length} 个三方应用。`);
}));
$('settings').addEventListener('submit', event => {
  event.preventDefault();
  run(async () => {
    const canonical = await control('set', serialize(read()));
    const c = parse(canonical); saved = serialize(c); paint(c);
    message('配置已保存。进程内规则需重开目标应用；路径规则和全局兼容处理需重启设备，已有内核路径标记会保留到重启。');
  });
});
$('preview-paths').addEventListener('click', () => run(async () => {
  const draft = await control('validate', serialize(read()));
  const plan = await control('path-plan', draft);
  $('path-plan').textContent = plan.trim() || '当前配置没有匹配的路径。';
  $('path-plan').hidden = false;
  message('已只读预览当前草稿的路径计划，没有保存或注册路径。');
}));
$('export').addEventListener('click', () => run(async () => {
  const text = await control('validate', serialize(read()));
  $('export-text').value = text;
  $('export-panel').hidden = false;
  $('export-panel').scrollIntoView({ block: 'center' });
  message('已生成当前草稿的配置文本；可复制或下载，导出不会修改设备配置。');
}));
$('copy-export').addEventListener('click', async () => {
  const text = $('export-text');
  try {
    await navigator.clipboard.writeText(text.value);
    message('配置文本已复制。请粘贴保存为 lineage-hide.conf。');
  } catch {
    text.focus(); text.select();
    if (document.execCommand('copy')) message('配置文本已复制。请粘贴保存为 lineage-hide.conf。');
    else message('自动复制不可用；配置文本已选中，请长按复制。', true);
  }
});
$('download-export').addEventListener('click', () => {
  const url = URL.createObjectURL(new Blob([$('export-text').value], { type: 'text/plain' }));
  const a = document.createElement('a'); a.href = url; a.download = 'lineage-hide.conf';
  a.click(); setTimeout(() => URL.revokeObjectURL(url), 30000);
  message('已请求下载。若管理器没有显示文件保存提示，请使用上方的复制配置文本。');
});
$('import').addEventListener('click', () => $('file').click());
$('file').addEventListener('change', event => run(async () => {
  const file = event.target.files[0]; if (!file) return;
  event.target.value = '';
  if (file.size > 65536) throw new Error('配置不能超过 64 KiB');
  const text = await control('validate', await file.text());
  paint(parse(text)); message('已校验并导入草稿。检查应用范围和全局开关，再保存。');
}));
run(reload);
