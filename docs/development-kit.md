# 完整开发套件

从 SDK Release 下载本机平台的包。包内有 APP SDK、手册、示例和 `xtensa` 工具链。当前 ABI 为 4.1.0。

固件镜像和模拟器只在仓库里有 `contract/release-family.json` 时打进同一个包。没有这份清单时，发布包只有 SDK。

## 使用

下载、安装、烧录和模拟器步骤见 [使用说明](使用说明.md)。

## 维护

要连同固件和模拟器发布时，先放好 `contract/release-family.json`，ABI 必须和 `contract/lock.json` 一致。打包只校验已有产物，不编译固件。

```bash
bash release.sh --hosts linux-x64,windows-x64 --development-kit-dir /path/to/pinned-payloads
```

## 4.1 正式版的源码关系

`sdk-v4.1.0` 为正式版。固件和模拟器均绑定各自主分支的精确提交。SDK首先提交公开契约和功能，再提交最终配套产物哈希；模拟器快照的 `sdkCommit` 指向清单中 `components.simulator.sdk_contract_revision`，最终 SDK 提交另由包内 `release.json.commit` 标明。SDK 工具与发布元数据可继续更新，但公开契约哈希必须一致。

本版包含 SD 预读与连续音效；无缝切歌、解码/输出线程解耦不在本版范围。完整边界与验证说明见 [连续音效说明](continuous-audio.md)。
