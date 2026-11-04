#!/system/bin/sh
# Systemless adaptation of null07089/Lineage_Hide service.sh (GPL-3.0).
# Explicit opt-in; only runtime properties and bind-mounted command wrappers.
set -eu
command -v resetprop >/dev/null || { echo 'resetprop unavailable'; exit 1; }
command -v nsenter >/dev/null || { echo 'nsenter unavailable'; exit 1; }
# Refuse an already wrapped command instead of copying a wrapper into itself.
[ "$(od -An -tx1 -N4 /system/bin/service | tr -d ' \n')" = 7f454c46 ] || {
    echo 'service is not an original ELF binary; disable conflicting wrappers and reboot'
    exit 1
}
mkdir -p /dev/lineage_hide
mkdir /dev/lineage_hide/legacy.once || { echo 'Legacy setup already attempted this boot'; exit 1; }

cat > /data/system/pm <<'WRAPPER'
#!/system/bin/sh
[ "$(/system/bin/id -u)" -lt 10000 ] && exec /system/bin/cmd package "$@"
/system/bin/cmd package "$@" | /system/bin/sed 's/lineage/lineagx/g; s/Lineage/Lineagx/g'
WRAPPER
cat /system/bin/service > /data/system/service_
cat > /data/system/service <<'WRAPPER'
#!/system/bin/sh
[ "$(/system/bin/id -u)" -lt 10000 ] && exec /data/system/service_ "$@"
/data/system/service_ "$@" | /system/bin/sed 's/lineage/lineagx/g; s/Lineage/Lineagx/g'
WRAPPER
for command_name in pm service; do
    source=/system/bin/$command_name
    target=/data/system/$command_name
    chmod "$(stat -c '%a' "$source")" "$target"
    chown "$(stat -c '%u:%g' "$source")" "$target"
    chcon "$(stat -c '%C' "$source")" "$target"
done
chmod "$(stat -c '%a' /system/bin/service)" /data/system/service_
chown "$(stat -c '%u:%g' /system/bin/service)" /data/system/service_
chcon "$(stat -c '%C' /system/bin/service)" /data/system/service_
nsenter -m -t 1 -- mount --bind /data/system/pm /system/bin/pm
nsenter -m -t 1 -- mount --bind /data/system/service /system/bin/service

check_reset_prop() {
    value=$(resetprop "$1")
    if [ -n "$value" ] && [ "$value" != "$2" ]; then resetprop -n "$1" "$2"; fi
}
contains_reset_prop() {
    case "$(resetprop "$1")" in *"$2"*) resetprop -n "$1" "$3" ;; esac
}
resetprop -n ro.boot.flash.locked 1
check_reset_prop ro.boot.verifiedbootstate green
check_reset_prop ro.debuggable 0
check_reset_prop ro.force.debuggable 0
check_reset_prop ro.secure 1
check_reset_prop ro.adb.secure 1
check_reset_prop ro.build.type user
check_reset_prop ro.build.tags release-keys
contains_reset_prop ro.bootmode recovery unknown
resetprop -n ro.boot.vbmeta.device_state locked
resetprop -n ro.boot.vbmeta.avb_version 1.4
resetprop -n ro.boot.vbmeta.hash_alg sha256
resetprop -n ro.boot.vbmeta.size 4096
resetprop -n init.svc.adb_root ''
resetprop -n ro.config.ringtone unknown

# Property data stays data; no shell commands are assembled from property values.
getprop | sed -n 's/^\[\([^]]*\)\]: \[\(.*\)\]$/\1=\2/p' |
while IFS='=' read -r name value; do
    case "$name" in
        *lineage*) resetprop --delete "$name" ;;
        *) case "$value" in
            *lineage*) replacement=$(printf '%s' "$value" | sed 's/lineage_//g; s/lineage//g')
                       resetprop -n "$name" "$replacement" ;;
           esac ;;
    esac
done
# SUSFS registration is independently handled by configctl apply-paths.
# Disable this option and reboot to clear these bind mounts/runtime properties.
