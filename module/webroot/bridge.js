// KernelSU's documented callback contract, with bounded UI waiting.
// https://github.com/tiann/KernelSU/blob/main/js/index.js
let sequence = 0;
export function exec(command) {
  return new Promise((resolve, reject) => {
    if (!window.ksu || typeof window.ksu.exec !== 'function') {
      reject(new Error('请从 KernelSU / 兼容管理器打开模块 WebUI。')); return;
    }
    const callback = `lh_exec_${Date.now()}_${sequence++}`;
    const timer = setTimeout(() => {
      window[callback] = () => { delete window[callback]; };
      reject(new Error('命令超时，执行结果未知。请重新读取配置确认。'));
    }, 15000);
    window[callback] = (errno, stdout, stderr) => {
      clearTimeout(timer); delete window[callback];
      if (Number(errno) !== 0) reject(new Error(String(stderr || stdout || `命令失败 (${errno})`).trim()));
      else resolve(String(stdout ?? ''));
    };
    try { window.ksu.exec(command, '{}', callback); }
    catch (error) { clearTimeout(timer); delete window[callback]; reject(error); }
  });
}
export const quote = value => `'${String(value).replaceAll("'", "'\\''")}'`;
export const control = (command, value) => exec(
  '/data/adb/modules/lineage_hide/bin/configctl ' + quote(command) +
  (value === undefined ? '' : ' ' + quote(value)));
