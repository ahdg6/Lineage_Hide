# Device validation — partial

## Preview.6 staged backend check — 2026-11-04

The installed module remained preview.5. The new arm64 configctl was staged separately and applied the unchanged saved configuration, without replacing the Zygisk library, installing a module, altering native ksud JSON or rebooting. Device: G57 Power / Evolution X / Android 16 (API 36), enforcing SELinux, local2 kernel `6.6.87-android15-8-4k-local2-g88d70c0b44bf-dirty`, BakaSU/SUSFS 2.3.0. Duck stayed at `2026.10.31-cf54fa7e6f26 (1099)`.

Earlier on November 4, preview.5 still reported 64 paths/3 maps while Duck found 15 file traces; an unchanged-config reboot cleared them. In this controlled follow-up, `sync; echo 2 > /proc/sys/vm/drop_caches` returned successfully. It requests dentry/inode cache reclamation globally; individual inode eviction events were not traced. To rescan a living Duck process, `am start` used NEW_TASK | CLEAR_TASK to recreate its activity/task without force-stopping the process. PID and `/proc/PID/stat` start time were checked, and the displayed scan timestamp changed. A cached result screen was not counted as another test.

| Scan time (Hong Kong) | Backend / process | Native files | Resource maps |
| --- | --- | --- | --- |
| 23:12:05 | Installed preview.5, PID 11230 | Clean | Clean |
| 23:23:13 | Preview.5 after cache reclaim, same PID 11230 | **14 traces**, Evolution-X/LineageOS | Clean |
| 23:24:22 | Staged preview.6, new PID 27993, first scan | Clean | Clean |
| 23:25:56 | Preview.6 after cache reclaim, same PID 27993 | Clean | Clean |
| 23:26:57 | Preview.6 after another cache reclaim, new PID 28842, first scan | Clean | Clean |

The new helper reported 64 path/3 map registrations and 65 retained references (the union includes a map-only file). After the app's post-reclaim scan, live status still reported `held`, unchanged native configuration, and `paths_visibility=unverified`. The latter is deliberate: the helper does not convert its own health result into an application visibility verdict. Duck's native library was Loaded in all recorded reports. The property-area probe remained Unavailable, so Clean file/map probes are not full Custom ROM coverage or evidence of stock firmware.

A sample of the sleeping, single-thread keeper showed 1,836 KiB RSS and no increase in user/system CPU ticks between the two samples around the ongoing-process check. This is a short observation, not a battery-life, OOM-survival or frame-time benchmark. Held inode/dentry metadata also consumes kernel memory and is not included in process RSS. See [PERFORMANCE.md](PERFORMANCE.md).

After testing, the staged keeper was stopped, the staged binary removed, original runtime receipt/log files restored, and temporary Shell root revoked through the manager UI (a fresh `su` call was denied). Saved module configuration was unchanged. Raw reports, scan timestamps, process/stat snapshots, helper receipts, command results and binary/config hashes are kept locally under `out/device-preview6/`; full Duck exports are also in the phone's Download directory. This staged test does not validate the final ZIP's boot service ordering, install/reboot lifecycle, all app scopes or other ROMs. The keeper must stay alive and the mounted file objects must remain the same; there is no watchdog or periodic re-marking. The installation/reboot and broader acceptance checks below remain necessary.

## Earlier device checks


A live check on 2026-11-02 used a G57 Power running Android 16 (API 36), Evolution X 16.0, Zygisk Next 1.5.2 and BakaSU/SUSFS 2.3.0. The installed preview.2 had no system-package snapshot: SELinux denied system_server writes to the `device:s0` output file passed through `cmd package`. Capturing through a pipe and publishing from the module shell produced a valid 495-package snapshot. Preview.3 includes that script fix; a reboot with the new ZIP has not yet been tested.

After publishing the snapshot and restarting Duck Detector, logs confirmed hooks in its main and helper processes. Its Custom ROM report showed package/service/reflection/native-file checks clean, with one remaining framework-resource mapping heuristic across system/product/data devices. Root inspection found the ROM's framework resource APKs, product overlays and resource-cache idmaps; this signal also appeared in the earlier upstream-module report. SUSFS reported 64 paths and 2 maps registered. These observations do not establish all channel/scope combinations, universal ROM compatibility or absence of every detection signal.

A follow-up isolated the five matching map rows: the main framework APK on the system device, `Pixelframework-res.apk` on the product device, and its resource-cache idmap on the data device. Duck 1099's pinned source uses a `framework-res.apk` substring match and flags three devices. Registering only `/product/overlay/Pixelframework-res.apk` with `add_sus_map` changed `mapsInjection` to Clean and the Custom ROM category to Info (`ROMs: None`, `Runtime: None`, `Native: None`). The property-area probe remained unavailable; this is not evidence of stock firmware or full detector coverage. The APK hash and permissions were unchanged and Duck restarted normally. The rule was then saved through configctl, with 64 paths and 3 maps reported applied. Preview.4 adds it to the EvoX preset without changing existing configurations or broadening path validation.

The full acceptance matrix below remains pending except for the observations above. Host/browser checks and successful NDK compilation are not device acceptance.

1. Disable other Lineage Hide variants, install this ZIP, reboot with a compatible Zygisk implementation, and open the WebUI. Confirm the detected preset, default-on native path option, default-off legacy option, installed app listing and valid boot snapshot.
2. Save include mode with one non-system test application. Force-stop/reopen it and another unlisted application. Check actual feature/service/resource/broadcast/reflection behavior in both; confirm the scope decision in `logcat -s LineageHide` where available.
3. Repeat in exclude mode, with empty include/exclude lists, master disabled, and an app using a secondary/custom-named process. Verify configured package selection also under a secondary Android user.
4. Confirm Settings, SystemUI and a vendor system application remain unaffected even if entered in the include list. Isolated and SDK-sandbox processes must remain untouched.
5. Exercise each channel toggle and a ROM-specific custom rule independently. Confirm resource loading, app startup and Binder error behavior remain stable. A clean UI status is insufficient evidence of successful JNI hooks.
6. Update the module and reboot. Confirm config persistence and that no upstream update URL is offered. Check malformed configuration skips injection and that a reviewed WebUI draft can repair it.
7. Confirm companion access to `/data/adb/lineage_hide/config.conf` and `/dev/lineage_hide/system_packages.txt` under the actual manager/SELinux policy; check that an unavailable companion skips injection without app crashes or boot delay accumulation.
8. On the supported BakaSU/SUSFS backend, preview the configured files, confirm missing-file reporting, save and reboot, then inspect `configctl status`, `/dev/lineage_hide/paths.status` and `paths.log`. Verify a responding keeper separately from the historical counts; confirm its loss is reported. Repeat fresh-process and same-process native reads after controlled cache reclaim. Verify actual path/map views in the target process and in excluded/root/system processes; do not assume the app scope restricts kernel marks. Confirm unsupported versions stop without registration, and a partial failure reports successful counts. Turn path handling off, reboot, and verify old marks are cleared independently of any other modules.
9. Verify configuration export/import in the actual manager WebView. Browser Blob downloads may need the documented root-file fallback.

The global legacy script is a separate opt-in test surface. Assess its source against the target device and record the original runtime properties and inspect conflicting command wrappers before enabling it. App exclusions do not limit global changes. Its successful exit does not prove the actual app-visible behavior is correct; inspect `/dev/lineage_hide/legacy.log`. No automated test here runs it on a phone.
