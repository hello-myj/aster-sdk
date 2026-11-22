# 配套组件

需要下载成品时，使用[SDK Releases](https://github.com/hello-myj/aster-sdk/releases)；需要系统媒体服务或新架构简称规则时，使用本仓库源码及匹配固件。源码版本、可下载开发包与设备商城是三条独立的发布路径。

已发布的 sdk-v4.3.0 对应 ABI 4.3，提供 Windows x64 和 Linux x64 开发包。配套固件、桌面模拟器分别由各自仓库的 Release 提供，版本和 SHA256 固定在 `contract/release-family.json`。

`aster firmware list` 列出固件；`aster firmware download` 下载；`aster firmware flash` 校验后烧录。`aster sim` 获取桌面模拟器并传递启动参数。下载缓存位于 Windows LocalAppData/Aster/components 或 Linux ~/.cache/Aster/components。

SDK 开发包附带固定版本的组件引用，按需下载配套工具。使用方法见 [开发包使用说明](使用说明.md)。
