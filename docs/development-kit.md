# 配套组件

下载对应主机的 [SDK 4.5.0](https://github.com/hello-myj/aster-sdk/releases/tag/sdk-v4.5.0)。新工程可直接使用包内的媒体 API、架构和板型清单功能。

SDK、固件 4.5.0 和 Windows 模拟器 1.1.0 使用 ABI 4.5。配套二进制由各自仓库发布，版本、下载地址和 SHA256 固定在 `contract/release-family.json`。

`aster firmware list` 列出固件；`aster firmware download` 下载；`aster firmware flash` 校验后烧录。`aster sim` 获取桌面模拟器并传递启动参数。下载缓存位于 Windows LocalAppData/Aster/components 或 Linux ~/.cache/Aster/components。

SDK 开发包附带固定版本的组件引用，按需下载配套工具。使用方法见 [开发包使用说明](使用说明.md)。
