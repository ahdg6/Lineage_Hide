# Native SUSFS adapter and inode lifetime

Preview.6 keeps the existing Zygisk hooks, presets, package scope and WebUI. It changes the native backend from one-shot inode marking to registration with retained file references. It does not use `add_sus_path_loop`, patch a kernel, change native ksud JSON or add a parallel registrar.

## Why the reference holder exists

In the reviewed local2/SUSFS 2.3.0 sources, `susfs_add_sus_path` and `susfs_add_sus_map` resolve a path, set an `i_mapping` flag and release the path reference. `fs/readdir.c` uses `ilookup` and preserves the directory entry when the inode is absent from cache. A recreated inode need not retain the old mapping flag. The November 4 report found 15 Duck native-file traces despite the old 64-path/3-map receipt, then zero after reboot without changing configuration. It did not capture those specific evictions or prove restoration I/O was the sole trigger. A later controlled reclaim request reproduced 14 traces in the same Duck process on preview.5; the staged preview.6 holder kept the file/map checks Clean in new and ongoing process tests. See the exact scope in [device validation](DEVICE-VALIDATION.md).

`add_sus_path_loop` queues re-marking on selected app creation events; it does not guarantee completion before the first read or maintain already-running applications continuously. This backend instead acquires an `O_PATH | O_CLOEXEC` descriptor for every planned path/map file **before** issuing any registration commands. Linux's `do_dentry_open` retains the path even for O_PATH, and the inode shrinker excludes referenced inodes. See the [O_PATH interface](https://man7.org/linux/man-pages/man2/open.2.html) and the reviewed Android common 6.6 `fs/open.c`, `fs/inode.c`, `fs/readdir.c` sources.

After registration, a child inherits the exact descriptors and exclusive operation lock. The parent waits for an acknowledgement after the child checks its references and publishes the receipt, then releases its own copies. There is no interval with neither process holding the references. Both hidden files and map-only files are retained. Duplicate paths across the two lists share one descriptor.

The `lh-path-keeper` process blocks waiting for explicit status/stop requests on a private Unix socket. It performs no timed scanning, re-registration, app-start polling or wake-lock acquisition. O_PATH retains file metadata, not the file contents in RAM. The process and references do consume memory; this is a deliberate lifetime-management cost, not a zero-overhead claim.

## Bounds, compatibility and failures

The command contract is based on [BakaSU 516d3e8](https://github.com/Baka-SU/BakaSU/blob/516d3e87fbc986ccefdde2c2765f1700612441a9/userspace/ksud/src/android/susfs/cli.rs). It requires a root-owned executable non-symlink `/data/adb/ksud`, exact `2.3.0` / `v2.3.0`, SUS_PATH and SUS_MAP when maps are configured. Commands use argument vectors and retain their eight-second individual/60-second shared budget.

Plans scan five partition roots, at most eight levels below each root, 100,000 entries and five seconds. Each list is limited to 512 entries. Reference acquisition permits at most 1,024 distinct paths, checks the process descriptor budget, rejects symlinks component by component and requires root-owned regular files on a read-only mount. Writable systemless replacements are rejected rather than treated as immutable ROM files. Missing explicit files are reported/skipped during planning. A file disappearing between planning and acquisition fails the operation before registration.

The backend checks each pathname against its retained device/inode before and after registration. If registration is partial, references remain held for the planned files and the successful counts remain explicit. A failure to start/acknowledge the keeper returns an error, even if registration commands already succeeded. The registration receipt remains historical; it is not a live health assertion. Repeated apply calls cannot create duplicate keepers or overwrite the active keeper's receipt.

The service starts registration before waiting for PackageManager. This reduces the initial boot window but does not hide paths before registration finishes. Reference retention addresses cache eviction on the same mounted files; it is not a universal policy for root-driven file replacement, mount namespace changes, a killed keeper or another component clearing flags. Status checks detect a missing/unresponsive keeper and changed path bindings. No watchdog silently restarts it. Reboot to apply changed rules or restore a failed keeper. Root-only `configctl stop-paths`, used by module uninstall, releases references; old marks are not guaranteed cleared until reboot.

## Three distinct observations

| Output | Meaning |
| --- | --- |
| `paths_status`, `paths_count`, `maps_count` | Latest registration command outcome/counts, including partial success. They do not count currently hidden files. |
| `paths_lifetime` | Live reference-holder result: `held`, `changed`, `unavailable`; `unmanaged` for old one-shot receipts; `inactive` when no holder was needed/started. |
| `paths_pinned_count` | Number of retained file references, which can differ from registration counts. |
| `paths_config_current` | Whether saved native-path settings match the active keeper; unrelated app/hook settings do not invalidate it. |
| `paths_visibility=unverified` | The privileged helper has not measured the target application's view. It must not turn a successful registration or health check into a Clean verdict. |

Status is queried only when requested. A health query rechecks pathname bindings without re-marking; it can warm metadata caches, so device visibility tests should probe the target app before requesting health again. The socket is mode 0600 in the mode-0700 runtime directory and checks peer UID. Client status waits and server request I/O are bounded. A stale receipt cannot impersonate a responding keeper after process loss.

Package lists govern Zygisk hooks only. Kernel path/map visibility still follows the reviewed kernel and manager unmount policy; excluded/system processes are not universally guaranteed an original view. The EvoX map-only rule for `Pixelframework-res.apk` remains unchanged and does not unload resources or alter backing devices.

## Validation

Host tests exercise actual O_PATH descriptors, fork handoff, fresh and ongoing status clients, duplicate start rejection, stale configuration, unlink/recreate detection, normal stop, forced process loss, publication failure, old receipts, descriptor budgets and symlink rejection under ASan/UBSan. They do not emulate SUSFS or establish app-visible hiding. Browser tests distinguish historical counts from missing maintenance and unverified visibility.

Device validation must separately record (1) a new app process's first native scan after registration is ready and (2) a scan in a continuously running app after controlled cache pressure. Record native probe availability, PID/uptime, holder health and exact scope. Global cache reclaim alone does not prove a specific inode was reclaimed. Single-device success does not establish every ROM, process namespace, memory-pressure/OOM condition or permanent hiding. See [DEVICE-VALIDATION.md](DEVICE-VALIDATION.md) for device observations and remaining work.
