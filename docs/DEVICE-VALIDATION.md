# Device validation — pending

These checks have not yet been performed for 3.0.0-preview.2. Host/browser checks and successful NDK compilation are not device acceptance.

1. Disable other Lineage Hide variants, install this ZIP, reboot with a compatible Zygisk implementation, and open the WebUI. Confirm the detected preset, default-on native path option, default-off legacy option, installed app listing and valid boot snapshot.
2. Save include mode with one non-system test application. Force-stop/reopen it and another unlisted application. Check actual feature/service/resource/broadcast/reflection behavior in both; confirm the scope decision in `logcat -s LineageHide` where available.
3. Repeat in exclude mode, with empty include/exclude lists, master disabled, and an app using a secondary/custom-named process. Verify configured package selection also under a secondary Android user.
4. Confirm Settings, SystemUI and a vendor system application remain unaffected even if entered in the include list. Isolated and SDK-sandbox processes must remain untouched.
5. Exercise each channel toggle and a ROM-specific custom rule independently. Confirm resource loading, app startup and Binder error behavior remain stable. A clean UI status is insufficient evidence of successful JNI hooks.
6. Update the module and reboot. Confirm config persistence and that no upstream update URL is offered. Check malformed configuration skips injection and that a reviewed WebUI draft can repair it.
7. Confirm companion access to `/data/adb/lineage_hide/config.conf` and `/dev/lineage_hide/system_packages.txt` under the actual manager/SELinux policy; check that an unavailable companion skips injection without app crashes or boot delay accumulation.
8. On the supported BakaSU/SUSFS backend, preview the configured files, confirm missing-file reporting, save and reboot, then inspect `/dev/lineage_hide/paths.status` and `paths.log`. Verify actual path/map views in the target process and in excluded/root/system processes; do not assume the app scope restricts kernel marks. Confirm unsupported versions stop without registration, and a partial failure reports successful counts. Turn path handling off, reboot, and verify old marks are cleared independently of any other modules.
9. Verify configuration export/import in the actual manager WebView. Browser Blob downloads may need the documented root-file fallback.

The global legacy script is a separate opt-in test surface. Assess its source against the target device and record the original runtime properties and inspect conflicting command wrappers before enabling it. App exclusions do not limit global changes. Its successful exit does not prove the actual app-visible behavior is correct; inspect `/dev/lineage_hide/legacy.log`. No automated test here runs it on a phone.
