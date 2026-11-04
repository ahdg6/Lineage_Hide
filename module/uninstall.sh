#!/system/bin/sh
# Stop the module-owned reference holder. Clearing all old marks needs reboot.
MODDIR=${0%/*}
"$MODDIR/bin/configctl" stop-paths >/dev/null 2>&1 || true
