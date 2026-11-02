# Lineage Hide · Configurable

English | [简体中文](README.zh-CN.md)

A configurable fork of [null07089/Lineage_Hide](https://github.com/null07089/Lineage_Hide), adding an offline KernelSU WebUI, editable LineageOS / Evolution X presets, per-package scope, channel controls and a native SUSFS path backend. GPL-3.0; upstream attribution is retained.

**3.0.0-preview.4:** arm64 compilation, native host tests and browser integration are covered. An Android 16/EvoX check confirmed injection after fixing a SELinux-related startup failure. Adding the EvoX overlay map rule cleared Duck's remaining resource-map finding; its unavailable property-area probe still limits coverage. Wider device/ROM compatibility still requires validation. A ready system-package snapshot is not evidence that hooks are active in a target app.

## Use

1. Install the built `Lineage_Hide.zip` in KernelSU or a compatible manager, enable a compatible Zygisk implementation, and reboot. Disable other Lineage Hide variants before testing this one.
2. Open the module WebUI. On first install, an Evolution version property/build identifier selects EvoX; otherwise the default is LineageOS. Existing configuration survives updates without being reset.
3. Choose a preset, package scope and channels, then save. Loading a preset changes rules while retaining the package list, scope, master switch and global compatibility setting.
4. Force-stop and reopen affected applications. Existing processes keep their startup configuration. Updating the module binary requires a device reboot.

The UI uses the [KernelSU WebUI contract](https://kernelsu.org/guide/module-webui.html). All assets are bundled; no CDN, remote scripts or runtime npm dependencies. A normal browser displays a missing-manager error and cannot save settings.

## Scope and rules

| Mode | Targets | Empty list |
| --- | --- | --- |
| All | All eligible third-party packages | List is ignored |
| Include / allowlist | Only listed third-party packages | No injection |
| Exclude / denylist | All eligible packages except listed packages | All eligible packages |

Packages are derived from Zygisk's `app_data_dir`, not customizable process names. Ordinary subprocesses share their owning package's rule. Rules apply to the same package across Android users; the picker lists installed third-party packages for the current user, with manual entry available for others.

Only appIds `10000..19999` are eligible. System packages, protected core components, isolated processes, SDK sandboxes and processes without a valid owning package are always skipped. The include list cannot override these protections. A fresh system-package snapshot is generated each boot in `/dev/lineage_hide`; injection is skipped until that snapshot and the configuration are valid.

Editable channels:

- Case-insensitive service substrings and exact service names.
- Case-sensitive exact feature names and protected broadcast actions.
- Resource package names and two-digit lowercase hexadecimal IDs.
- A toggle for the fixed `AssetManager.LINEAGE_APK_PATH` reflection filter.
- A toggle for Zygisk's `FORCE_DENYLIST_UNMOUNT` request.

The LineageOS preset contains the upstream Lineage SDK rules. EvoX inherits these rules and adds `evolution` / `evox` service substrings. These are editable starting rules, not evidence that all ROM versions expose those services or that every EvoX signal is covered. Other upstream ROM keywords can be added manually.

Process-local Binder/JNI hooks copy matched service replies into Parcel-owned storage before editing them; feature reads substitute returned strings without writing reply bytes. Resource-owned descriptors are never closed. Broadcast filtering targets only Intent.action and leaves extras unchanged. Native paths/maps use the separate SUSFS adapter below; other native scanning and hardware attestation remain outside the process hooks' scope. The package list does not control SUSFS, kernel patches or other modules.

## Native SUSFS paths

Both presets include editable filename keywords, explicit hidden files and mapping paths. `native_paths` defaults on; the master switch also gates execution. Saved rules apply next boot. Read-only preview evaluates the current draft without saving or marking paths. Preset changes preserve the user's path switch. Removing/disabling rules requires a reboot to clear previous inode marks.

The adapter currently requires `/data/adb/ksud susfs`, **SUSFS 2.3.0** and the relevant enabled kernel features. Unsupported devices report that state while retaining process-local filtering; no legacy-script fallback is executed. The adapter never calls `ksud susfs config` or changes the manager's persistent JSON.

Only regular files under `/system`, `/system_ext`, `/product`, `/vendor`, `/odm` are eligible. Symlinks are not followed. Keywords match basenames, with explicit path validation rejecting traversal, directories, wildcards and shell syntax. Defaults include inherited Lineage paths such as `libstagefright.so`; review the preview before use. Maps are configured independently; scanned paths do not automatically become map rules. The EvoX preset also includes `/product/overlay/Pixelframework-res.apk` as a **map-only** rule, preserving resource loading. Existing configurations are retained on update: add this path under mapping rules, or load/review the updated EvoX preset and save. Missing files on other builds are skipped.

**Application allow/deny lists do not scope kernel marks.** Visibility depends on kernel/manager unmount policy. Status distinguishes unsupported, applied, failed and partially applied operations. Successfully registered marks survive a later failure until reboot. Logs are in `/dev/lineage_hide/paths.log`. The adapter has a limited Android 16/EvoX device check; see [backend details](docs/SUSFS-BACKEND.md).

## Global compatibility option

A systemless adaptation of the upstream boot script lives at `module/scripts/legacy-service.sh` and is **off by default**. It runs next boot only if both the master switch and `legacy_boot` are enabled. Its rules are fixed Lineage rules and do not follow the selected preset or custom entries.

This adaptation only rewrites runtime properties and bind-mounts command wrappers. Upstream system-partition edits, `build.prop` changes, Jelly disabling and duplicate SUSFS registration have been removed. App scopes do not limit these global actions. Disable and reboot to clear its runtime effects; historical disk/package modifications from an older upstream installation still require separate recovery. The adaptation still requires device validation.

Keep it disabled when first validating process-local filtering. Zygisk's own denylist policy must permit module loading; this module cannot override a manager that skips injection entirely.

## Persistent configuration

Settings live in `/data/adb/lineage_hide/config.conf` (directory `0700`, file `0600`), outside the replaceable module directory. The root companion reads them for each new process. A shared native parser validates bounded, versioned data; writes use a synced temporary file and atomic rename. Configurations are never sourced as shell scripts.

Import loads a validated draft; saving is explicit. Export validates the current draft and provides copyable text plus a download. Use text copying if a manager does not support Blob downloads; the saved file is also available to root file managers. Uninstall preserves configuration; back up and delete the state directory manually to remove it completely. Invalid configurations skip injection and are never silently broadened to all apps. WebUI recovery displays a default draft for review before saving. See [configuration format and CLI](docs/CONFIGURATION.md).

## Build and verify

Linux x86_64, NDK r27c / `27.2.12479018`, CMake, Ninja, Python 3, zip/unzip and readelf:

```sh
bash scripts/build-native.sh /absolute/path/to/android-ndk-r27c
# out/Lineage_Hide.zip
```

The direct native build needs no Gradle, JDK or full SDK. The optional Gradle project also builds both targets; pass the directory containing `liblineage_hide.so` and `configctl` to `scripts/package.sh`. Only arm64 is packaged. API 26 is the minimum link target, not a tested Android compatibility guarantee.

```sh
export JNI_HEADER=/absolute/path/to/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h
python3 -m unittest discover -s tests -v
npm ci
npx playwright install chromium
npm test
# Or CHROMIUM_PATH=/path/to/chromium npm test
```

Host tests cover parser bounds, persistence, upgrades, app scope, multi-user UIDs, protected packages, invalid snapshots, companion framing and filtering. Native tests use ASan/UBSan. A fake JNI/Parcel harness executes the actual hook code for reply ownership, allocation failure, interface transaction collisions, broadcast encodings/extras, reflection scope and partial resource installation. The command-runner tests use a local fake executable to exercise output limits, timeouts and child cleanup. Boot-script tests isolate all device paths. These fixtures do not simulate Android Binder or ART internals. Browser integration runs the real host configuration tool behind a simulated KernelSU bridge, with a mocked app list. It does not substitute for [device validation](docs/DEVICE-VALIDATION.md).

Preview builds omit automatic updates to prevent replacement by an upstream release. Tagged builds use this fork's own update manifest. Original kernel patch helpers remain under `scripts/`; they are not configured by the WebUI.
