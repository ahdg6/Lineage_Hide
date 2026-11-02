export const flags = ['enabled', 'services', 'features', 'broadcasts', 'resources', 'reflection', 'force_unmount', 'native_paths', 'legacy_boot'];
export const lists = ['package', 'service_keyword', 'exact_service', 'feature', 'broadcast', 'resource_package', 'resource_id', 'path_keyword', 'hide_path', 'map_path'];
export function parse(text) {
  const result = Object.fromEntries(lists.map(key => [key, []]));
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const index = line.indexOf('=');
    if (index < 1) throw new Error('配置格式错误');
    const key = line.slice(0, index), value = line.slice(index + 1);
    if (lists.includes(key)) result[key].push(value);
    else if (flags.includes(key)) result[key] = value === '1';
    else result[key] = value;
  }
  return result;
}
export function serialize(config) {
  const rows = ['schema=1', `profile=${config.profile}`, `mode=${config.mode}`];
  for (const key of flags) rows.push(`${key}=${config[key] ? 1 : 0}`);
  for (const key of lists) for (const value of config[key]) rows.push(`${key}=${value}`);
  return rows.join('\n') + '\n';
}
export function lines(text) { return text.split(/\r?\n/).map(x => x.trim()).filter(Boolean); }
