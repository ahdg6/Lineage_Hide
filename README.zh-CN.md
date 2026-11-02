# Lineage Hide · Configurable

[English](README.md) | 简体中文

基于 [null07089/Lineage_Hide](https://github.com/null07089/Lineage_Hide) 的可配置 fork。提供 KernelSU WebUI、LineageOS / Evolution X 预设、应用黑白名单、自定义进程内规则及原生 SUSFS 路径后端。保留上游 GPL-3.0 许可证及作者署名。

**当前版本：3.0.0-preview.4。** 已完成 arm64 编译、主机 native 测试及浏览器与真实配置工具的联调；在 Android 16/EvoX 实机上定位并修复了 SELinux 导致的启动清单失败，已确认 Duck 主进程及辅助进程注入；补充 EvoX 覆盖 APK 的映射规则后，Duck 的 mapsInjection 已为 Clean；属性区探针仍不可用，完整兼容性验收尚未完成。WebUI 的“清单就绪”只表示启动清单存在，不代表目标应用已成功安装 hooks。

## 使用

1. 在 KernelSU / 兼容管理器安装构建出的 `Lineage_Hide.zip`，启用兼容的 Zygisk 实现并重启。不要同时启用其他 Lineage Hide 衍生模块。
2. 打开模块 WebUI。首次安装检测到 `ro.evolution.version` / `ro.evolution.build.version` 或 Evolution/EvoX 构建标识时使用 EvoX 预设，否则使用 LineageOS。已有配置跨升级保留，不重新探测或覆盖。
3. 选择预设、应用范围与隐藏通道，按“保存配置”。载入预设只更新规则，保留应用名单、范围、总开关和全局兼容开关。
4. 强行停止并重新打开目标应用，让其所有进程读取新配置。更新模块二进制本身必须重启设备。

UI 使用 [KernelSU WebUI 接口](https://kernelsu.org/guide/module-webui.html)，所有资源随 ZIP 打包，无 CDN、远程脚本或运行时 npm 依赖。普通浏览器只会提示缺少管理器接口，不能实际保存配置。

## 应用范围

| 模式 | 生效范围 | 空名单 |
| --- | --- | --- |
| 全部三方应用 | 所有符合条件的三方应用 | 名单不参与判断 |
| 仅名单内应用（白名单） | 名单中包名精确匹配的三方应用 | 不注入任何应用 |
| 排除名单内应用（黑名单） | 三方应用中排除名单内包名 | 覆盖全部三方应用 |

包名通过 Zygisk 提供的 `app_data_dir` 确定，不依赖可自定义的进程名称，因此通常的 `:remote` 子进程共用包规则。规则按包匹配，适用于不同 Android 用户中的同一包；选择器只展示当前用户的已安装三方包，其他包可手动填写。

仅普通应用 appId `10000..19999` 可注入。系统应用、核心组件、隔离 UID、SDK 沙箱、无法确定包归属的进程始终跳过；白名单不能覆盖这些保护。启动脚本生成 `/dev/lineage_hide/system_packages.txt`，没有有效清单时跳过注入。模块启动清单在每次重启后刷新。

## 可配置通道

| 通道 | 可编辑内容 |
| --- | --- |
| 系统服务 | 不区分大小写的子串关键词及完整服务名 |
| 系统特性 | 区分大小写的完整 feature 名称 |
| 受保护广播 | 区分大小写的完整 action 名称 |
| 资源包 | 完整包名与两位小写十六进制 package ID |
| 反射 | 开关固定的 `AssetManager.LINEAGE_APK_PATH` 过滤 |
| 模块挂载 | 是否请求 Zygisk `FORCE_DENYLIST_UNMOUNT` |

LineageOS 预设包含上游 Lineage 服务、SDK 特性、广播、资源和反射规则。EvoX 以相同 SDK 规则为基础，增加 `evolution` / `evox` 服务关键词；这些是可编辑起点，不表示每个 ROM 版本都存在对应服务，也不代表覆盖了所有 EvoX 特征。原版其他 ROM 关键词可在自定义规则中自行添加。

服务列表过滤先让 Parcel 复制 Binder reply 到自有缓冲区，再修改匹配项；feature 读取只替换返回字符串，不写 reply 字节。广播只处理 Intent.action，不改 extras。不关闭资源系统持有的 fd。原生路径与映射使用下方独立的 SUSFS 后端；其他原生扫描与硬件证明不由进程内 hooks 解决。应用名单不会改变 SUSFS、内核补丁或其他模块的作用范围。

## 原生 SUSFS 路径

两套预设同时带可编辑路径关键词、显式隐藏文件与映射路径。`native_paths` 默认开启，总开关关闭时不执行；保存后在下次启动应用，WebUI 的“只读预览”不会保存或注册路径。切换预设保留用户的路径开关选择。路径标记在本次启动持续有效，删除、关闭或修改规则后需重启清除旧标记。

当前后端仅支持 `/data/adb/ksud susfs` 提供的 **SUSFS 2.3.0** 接口及所需功能。没有该接口时报告不支持，进程内规则仍可使用，不会回退执行旧版全局脚本。路径处理不调用 `ksud susfs config`，不改写原生管理器的持久 JSON。

只扫描 `/system`、`/system_ext`、`/product`、`/vendor`、`/odm` 的普通文件，不跟随符号链接。关键词仅匹配文件名；显式路径逐段校验，拒绝越界、目录、通配符和 Shell 字符。预设沿用 Lineage 路径，包括 `libstagefright.so`；请先预览实际匹配项。映射列表独立配置，关键词扫描不自动添加映射规则。EvoX 预设另含 `/product/overlay/Pixelframework-res.apk` 的**仅映射规则**，保留资源加载。升级保留已有配置；需要此规则时可在映射列表中手动添加该路径，或重新载入并检查 EvoX 预设后保存。其他构建不存在此文件时跳过。

**应用黑白名单不控制这些内核标记。** 实际可见性由内核和管理器的卸载策略决定；界面会显示未执行、不支持、成功、失败或部分成功。失败后已注册的标记仍保留到重启，日志为 `/dev/lineage_hide/paths.log`。后端已在 Android 16/EvoX 上完成有限实测，范围与限制见 [SUSFS-BACKEND.md](docs/SUSFS-BACKEND.md)。

## 全局兼容开关

基于原版整理的 systemless 兼容脚本位于 `module/scripts/legacy-service.sh`，**默认关闭**，只有总开关和 `legacy_boot` 同时开启时才在下次启动执行。规则仍为原版固定 Lineage 规则，不读取自定义服务/特性规则，也不跟随 EvoX 预设变更。

此脚本只全局改写运行时属性，并 bind mount `pm` / `service` 包装器；已删除上游的系统分区写入、`build.prop` 编辑、Jelly 停用和重复 SUSFS 注册。关闭后重启清除本次挂载与属性处理。它不受应用黑白名单控制，也不应被理解为按应用的属性伪装。若过去安装过原版，其已有磁盘修改或应用停用状态仍需单独恢复。

保留兼容脚本不等于验证了它在当前设备上的可用性。初次试用建议保持关闭，先验证进程内处理。Zygisk 的 denylist 策略应允许模块被加载；如果管理器在注入前已排除目标应用，本模块名单不能绕过管理器决定。

## 配置持久化

配置在 `/data/adb/lineage_hide/config.conf`，目录 `0700`、文件 `0600`，更新 ZIP 不携带或覆盖用户配置。通过 Zygisk root companion 读取，应用进程不会直接读取 `/data/adb`。格式有版本号、数量及长度限制，写入前由 native 校验，临时文件同步后原子替换；不会作为 Shell 脚本执行。

WebUI 导入后先显示草稿，保存才落盘。导出的是当前已校验的草稿，界面提供配置文本复制及下载；部分管理器不支持 Blob 下载时可使用复制按钮，也可用 Root 文件管理器复制上述已保存文件。卸载保留此目录，彻底清除设置时手动备份并删除。损坏配置不会自动恢复为“全应用”模式；WebUI 会显示错误并提供可检查后保存的默认草稿。协议及 CLI 见 [配置说明](docs/CONFIGURATION.md)。

## 构建和测试

Linux x86_64，NDK r27c / `27.2.12479018`，CMake、Ninja、zip、unzip、readelf：

```sh
bash scripts/build-native.sh /absolute/path/to/android-ndk-r27c
# 输出 out/Lineage_Hide.zip
```

直接 native 构建不需要 JDK、Gradle 或完整 SDK。保留 Gradle 工程作为可选入口；若使用 Gradle，需将同时含 `liblineage_hide.so` 和 `configctl` 的构建目录传给 `scripts/package.sh`。仅打包 arm64，最低链接 API 26 不等同所有 Android 版本都已验证。

```sh
export JNI_HEADER=/absolute/path/to/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h
python3 -m unittest discover -s tests -v
npm ci
npx playwright install chromium
npm test
# 已有 Chromium 可使用 CHROMIUM_PATH=/path/to/chromium npm test
```

测试覆盖解析边界、原子保存、升级保留、多用户 appId、黑白名单、系统保护、配置损坏、companion 帧、过滤规则、路径计划、越界/链接拒绝及 UI 保存/导入/导出/错误处理。C++ 测试启用 ASan / UBSan。新增 JNI/Parcel 模拟层执行实际 hook 代码，覆盖回复缓冲区归属、内存分配失败、跨接口事务号冲突、广播编码与附加数据、反射范围及资源 hook 部分安装；命令执行器测试覆盖输出限制、超时和子进程清理，启动脚本测试隔离所有设备路径。模拟层不代替真实 Binder / ART。浏览器联调执行主机版配置工具，模拟 KernelSU 桥及应用清单；不替代 Android 端验收。实机检查项见 [DEVICE-VALIDATION.md](docs/DEVICE-VALIDATION.md)。

预览构建不含自动更新地址，避免意外更新回上游无配置版本。tag 发布构建使用当前 fork 的更新地址；不使用上游 Release manifest。
