# Runtime review — preview.6

The module does not tune CPU/GPU clocks, scheduling, thermal limits or power policy. It creates no polling thread, timer service or wakelock. The native path backend now keeps one idle `lh-path-keeper` process with bounded O_PATH references; it blocks on a private status socket and consumes memory for the process/file metadata, without timed rescans or re-marking. The Zygisk companion handles requests at process startup; the WebUI runs commands on load or user actions, with no periodic status polling.

## Work that remains

| Path | Work and limits |
| --- | --- |
| Boot | Acquire path references/register SUSFS paths, receive the keeper acknowledgement, publish the system-package snapshot, then exit the boot script. The reference holder remains alive. PackageManager has at most 60 attempts, each with a 2-second command timeout and 1-second retry sleep. |
| Native path preview/application | Scan at most 100,000 entries, 8 recursion levels below each root and 5 seconds; at most 512 paths and 512 maps. Application has a 60-second command-operation deadline and an 8-second cap per ksud command. No rescanning during app operation. |
| Eligible app startup | Read/validate configuration and the system-package snapshot through the companion, select package scope and install requested hooks once. These reads/parses remain per process; there is no background refresh. |
| Binder transactions | Unrelated transaction IDs and one-way calls immediately call the original function, without JNI inspection. Candidate IDs still validate the interface token because transaction IDs can collide. Only matching names trigger request copies/replacements. |
| Direct hidden service lookup | Caller stack classification still runs when the service name matches. It protects framework initialization and is not performed for ordinary service names or all Binder calls. |
| Parcel strings | If feature filtering is active, inspect string length and extract only lengths present in configured names. Matching still has a cost on this broad native read hook. |
| Resource/reflection hooks | Resource IDs use a 256-bit lookup; package names and the fixed reflection field reject impossible lengths before extraction. Unrelated reflected classes pass through. |
| Logging | One successful setup summary per injected process; errors remain logged. Per-hook details require the CMake option `LH_VERBOSE_LOGS=ON`, off by default. No per-call success logging. |

JNI name matching uses bounded stack storage (up to 512 UTF-16 code units plus a 512-byte ASCII result) without temporary native heap strings. Matching replacements still allocate Java strings, and matched Binder rewrites still use owned Parcels. A disabled or empty channel installs no corresponding hooks. If all process channels are empty/off, including reflection, the module requests unloading after any configured unmount request. Native SUSFS marks continue to follow kernel/manager policy independently of package scope.

The opt-in legacy boot script remains disabled by default. When enabled it installs global `pm`/`service` wrappers, so invoking those commands has shell/process overhead. The verified EvoX mapping fix does not need this option. No default was changed and existing user configuration is preserved.

## Cleanup and evidence

Removed the standalone procfs and SELinux kernel-source patchers. Neither was included in the configurable module ZIP; deleting them cleans the repository and does not alter the installed kernel or claim to improve its performance. The packaged module continues to use the existing SUSFS backend without adding a kernel patch.

Host JNI regressions execute the production hook bodies under ASan/UBSan. They check that unrelated/one-way transactions do not invoke JNI inspection, empty channels install nothing, impossible name lengths skip extraction, ordinary strings are retained, and extraction failures leave original values usable. Existing checks cover interface collisions, Binder reply ownership, broadcast action boundaries and partial resource installation. Host tests simulate JNI/Parcel behavior and do not measure ART costs.

A staged device sample showed one sleeping keeper thread, 1,836 KiB RSS and no CPU-tick increase around the ongoing-process check; held inode/dentry kernel memory is additional. This is not a sustained benchmark. No device battery-life or frame-time comparison has been performed for preview.6. The earlier Duck result validates the mapping rule at that time, not indefinite marker lifetime or keeper performance. November 4 observations exposed the one-shot path lifetime limitation; preview.6 addresses it with retained references. A comparable reboot, app cold-start/interaction check and idle-power measurement on the same configuration are still required before making quantitative claims. Package include mode is available when only selected apps need process hooks; it does not narrow kernel marks.

Preview.6 deliberately adds a bounded resident process. A retained reference prevents ordinary inode eviction while the holder lives; it does not pin file data, disable reclamation globally, alter OOM policy or promise to survive the holder being killed. Status checks report loss instead of treating old registration counts as current success. See [SUSFS-BACKEND.md](SUSFS-BACKEND.md).
