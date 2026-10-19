# 完整开发套件

从 SDK Release 下载本机平台的包。包内有 APP SDK、手册、示例和 `xtensa` 工具链。当前 ABI 为 4.0.0。

固件镜像和模拟器只在仓库里有 `contract/release-family.json` 时打进同一个包。没有这份清单时，发布包只有 SDK。

## 使用

下载、安装、烧录和模拟器步骤见 [使用说明](使用说明.md)。

## 维护

要连同固件和模拟器发布时，先放好 `contract/release-family.json`，ABI 必须和 `contract/lock.json` 一致。打包只校验已有产物，不编译固件。

```bash
bash release.sh --hosts linux-x64,windows-x64 --development-kit-dir /path/to/pinned-payloads
```
