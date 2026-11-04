#!/system/bin/sh
ui_print "Lineage Hide • configurable fork"
[ "$ARCH" = arm64 ] || abort "Only arm64 is supported"
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755
set_perm "$MODPATH/scripts/legacy-service.sh" 0 0 0755
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644
# KernelSU manages webroot ownership/context itself.
profile=lineage
[ -n "$(getprop ro.evolution.version)" ] && profile=evox
[ -n "$(getprop ro.evolution.build.version)" ] && profile=evox
case "$(getprop ro.evolution.version) $(getprop ro.build.display.id) $(getprop ro.build.version.incremental)" in
    *[Ee]volution*|*[Ee]vo[Xx]*) profile=evox ;;
esac
"$MODPATH/bin/configctl" init "$profile" >/dev/null || abort "Invalid existing configuration; back up /data/adb/lineage_hide/config.conf before repairing it"
ui_print "- Configuration survives updates / 配置跨升级保留"
ui_print "- Open KernelSU WebUI to select preset and apps / 在 WebUI 中选择预设和应用"
ui_print "- Global legacy boot actions are OFF by default / 原版全局启动处理默认关闭"
ui_print "- Native SUSFS paths apply after reboot when supported / 原生路径规则在受支持内核重启后应用"
ui_print "- Reboot to load this version / 重启加载新版本"
