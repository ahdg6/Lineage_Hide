# Native SUSFS adapter

The adapter is based on the `ksud susfs` command contract in [BakaSU 516d3e8](https://github.com/Baka-SU/BakaSU/blob/516d3e87fbc986ccefdde2c2765f1700612441a9/userspace/ksud/src/android/susfs/cli.rs). It does not implement a generic `ksu_susfs` executable or LKM `/proc` fallback.

Before registering anything, it requires a root-owned, executable, non-symlink `/data/adb/ksud`, an exact `2.3.0` / `v2.3.0` version response, `CONFIG_KSU_SUSFS_SUS_PATH`, and `CONFIG_KSU_SUSFS_SUS_MAP` if the plan includes mapping files. Calls use `fork`/`execv` argument vectors, never shell interpolation.

The generated plan includes regular files whose basenames match configured substrings, plus explicit file paths. Scanning is limited to five partition roots, eight directory levels below each root, 100,000 visited entries and a five-second traversal budget. It does not follow symbolic links. Explicit files are checked component by component and rechecked immediately before registration. Empty keyword lists skip scanning. Missing explicit files are reported and skipped; malformed or symlink paths stop the plan. Each hidden-path and map list is limited to 512 entries.

Registration commands have an eight-second per-command timeout and a shared 60-second operation budget; command output is capped at 16 KiB. A runtime lock prevents overlapping applications. No resetprops, package disables, mounts, on-disk partition edits or changes to native `ksud` JSON are part of this adapter.

The boot service applies saved rules when both `enabled` and `native_paths` are true. WebUI only previews and saves. The preview reports intended matching files and does not establish backend availability or actual app visibility. Unsupported adapters are reported without running the legacy script as a fallback.

Kernel registration is additive for the current boot, not transactional. A failure after some successful registrations reports `partial`, including successful counts. `/dev/lineage_hide/paths.log` contains the plan/error and `paths.status` contains the latest result. Neither a failed operation nor turning the switch off removes previously registered marks; reboot after changing/removing rules. Other modules can independently reapply their own marks.

App allow/deny lists only govern the Zygisk hooks. They cannot remove a package from the kernel's path policy. Actual kernel filtering depends on the supported kernel and root-manager unmount policy. This fork does not claim that system applications or excluded packages universally retain original file views.

Host tests cover parser/path validation, filesystem scanning, link skipping, bounds and unsupported-backend behavior. The production command-runner code is also tested against a local substitute executable for argument preservation, exit status, output limits, closed pipes, deadlines and descendant cleanup. They deliberately never execute a phone's root daemon. Real `ksud` execution, SELinux access and app-visible results remain part of device validation.

The EvoX preset adds `/product/overlay/Pixelframework-res.apk` only to the mapping list. On the tested EvoX build, this avoids Duck grouping its two map rows with the main framework APK and an idmap into a three-device heuristic. SUSFS filters the map view for processes covered by its unmount policy; the overlay remains loaded and its on-disk contents are unchanged. It is not a per-package rule and does not alter the ROM's actual backing devices. Existing user configurations are not migrated automatically.
