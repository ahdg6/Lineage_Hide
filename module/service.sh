#!/system/bin/sh
# Publish system-package scope, apply the configured SUSFS adapter, then optional legacy actions.
MODDIR=${0%/*}
CTL="$MODDIR/bin/configctl"
RUNTIME=/dev/lineage_hide
umask 077
mkdir -p "$RUNTIME" || exit 1
chmod 0700 "$RUNTIME"
echo disabled > "$RUNTIME/legacy_boot"
profile=lineage
[ -n "$(getprop ro.evolution.version)" ] && profile=evox
[ -n "$(getprop ro.evolution.build.version)" ] && profile=evox
case "$(getprop ro.evolution.version) $(getprop ro.build.display.id) $(getprop ro.build.version.incremental)" in
    *[Ee]volution*|*[Ee]vo[Xx]*) profile=evox ;;
esac
"$CTL" init "$profile" >/dev/null || exit 1
# No fallback injection while PackageManager is unavailable. Retry <= 3 min.
attempt=0
while [ "$attempt" -lt 60 ]; do
    # cmd passes stdout to system_server over Binder. A direct redirect to a
    # device:s0 file under /dev is denied even when this shell runs as root.
    # Capture through a pipe, then let the module shell write its private file.
    if packages=$(/system/bin/toybox timeout -s KILL 2 /system/bin/cmd package list packages -s 2>/dev/null); then
        printf '%s\n' "$packages" | sed -n 's/^package://p' | sort -u > "$RUNTIME/packages.tmp"
        if grep -qx android "$RUNTIME/packages.tmp"; then
            chmod 0600 "$RUNTIME/packages.tmp"
            mv -f "$RUNTIME/packages.tmp" "$RUNTIME/system_packages.txt"
            break
        fi
    fi
    attempt=$((attempt + 1))
    sleep 1
done
unset packages
rm -f "$RUNTIME/packages.tmp"
"$CTL" apply-paths || echo 'SUSFS path application failed; see KernelSU WebUI status'
if "$CTL" legacy-boot; then
    echo started > "$RUNTIME/legacy_boot"
    /system/bin/sh "$MODDIR/scripts/legacy-service.sh" > "$RUNTIME/legacy.log" 2>&1
    echo "finished_exit_$?" > "$RUNTIME/legacy_boot"
fi
