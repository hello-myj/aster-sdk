# 配套组件

SDK Release 提供 Windows x64 和 Linux x64 开发包。配套固件、桌面模拟器分别由各自仓库的 Release 提供，版本和 SHA256 固定在 `contract/release-family.json`。

`aster firmware list` 列出固件；`aster firmware download` 下载；`aster firmware flash` 校验后烧录。`aster sim` 获取桌面模拟器并传递启动参数。下载缓存位于 Windows LocalAppData/Aster/components 或 Linux ~/.cache/Aster/components。

SDK 开发包附带固定版本的组件引用，按需下载配套工具。使用方法见 [开发包使用说明](使用说明.md)。
