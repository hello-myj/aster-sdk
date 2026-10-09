# SDK 发布：Ubuntu 一次打包 Windows 和 Linux

推荐从一个 Ubuntu x86_64 工作区发布两个主机包。Windows 保留 `aster.exe`、`aster-toolchain.exe`、`aster-device.exe`，使用便携目录 ZIP；Linux 使用目录 tar.gz。Windows 不需要安装 Python 或 WSL；Linux 需要与打包环境相同的 Python 小版本，当前发布流程为 Python 3.12，依赖已随包提供。当前验证环境为 Ubuntu 22.04 / glibc 2.35、Windows x64，CI 使用 Ubuntu 24.04。

## 接口与编译器分开更新

```mermaid
flowchart LR
  F[固件公开接口] --> C[导出契约 ZIP]
  C --> V[清单、SHA256、ABI 校验]
  V --> S[SDK 头文件、导入符号、板卡信息]
  S --> P[Ubuntu 发布入口]
  B[板卡到 AOT 工具链配置] --> P
  T[固定编译器包和依赖缓存] --> P
  P --> W[Windows EXE 与便携运行时 ZIP]
  P --> L[Linux 工具 tar.gz]
```

固件仓库负责导出 `contract/lock.json`、`contract/api.json`、`include/aster/` 和 `targets/`。SDK 导入这些公开文件，不读取固件内部源码。添加接口后，新的公开声明、导入符号、ABI 与能力信息随契约进入 SDK。工具从这些数据读取规则，普通接口增加无需重编 Windows EXE 启动器以外的原生编译器。

发布包中的模板 ABI 自动跟随契约，并生成 `docs/api-index.md`。函数的用途、单位、生命周期和示例仍需在固件公开头文件或手册中维护；只有符号和签名不能推导这些语义。新的通信协议、打包格式或特殊功能逻辑仍需要修改对应工具代码。

`toolchains/catalog.json` 独立定义“开发板 → CPU/AOT 后端 → 编译器配置 → 主机二进制及校验值”。**主机架构决定编译器在哪里执行，板卡架构决定编译器输出什么 AOT。** 当前登记的是 `lckfb-s3` 与 `waveshare-35b` 两块板：同为 Xtensa / AOT 格式 6，共用同一编译器配置 `esp32s3-wamr-2.4.3-v6`。**每个主机平台只发布一个便携包，包内携带全部已登记开发板；构建与烧录用 `--target` 选板，编译器由所选板卡自动决定。** 包内允许有多个编译器配置，但必须共用同一 wasi-sdk 版本，且一个 AOT 后端只能对应一个配置（否则 `--target` 无从区分）。多后端（如日后新增 RISC-V）时，各后端的 `wamrc` 放在包内 `toolchain/backends/<backend>/`；只有一个编译配置的包保持平铺的 `toolchain/wamrc[.exe]` 布局。

## 一次性准备

Ubuntu 安装 Python 3.12、venv 和 `gcc-mingw-w64-x86-64`，然后创建发布环境：

```bash
python3.12 -m venv .venv-linux
.venv-linux/bin/python -m pip install -r requirements.txt
```

仓库提供 Linux 编译器。Windows 编译器作为固定依赖包复用，不能把 Linux 的 `wamrc` 当作 Windows 程序使用。已有 Windows 编译器缓存可在 Ubuntu 中导出一次：

```bash
.venv-linux/bin/python tools/toolchain_bundle.py \
  --target xtensa \
  --output dist/toolchains/windows-x64-esp32s3-wamr-2.4.3-v6.zip
```

该命令要求配置中的 Windows `wasi` 和 `wamrc` 文件已准备好。首次获得或重编这两个原生工具是工具链维护工作；后续 ABI 更新复用该 ZIP。把它存入团队制品库，并保存命令输出的 SHA256。新 Ubuntu 机器只需下载这个编译器包和 SDK，无需到 Windows 克隆 SDK 或重新打包。

## 日常发布命令

```bash
bash release.sh \
  --contract /path/to/aster-contract.zip \
  --hosts linux-x64,windows-x64 \
  --windows-toolchain-bundle /path/to/windows-x64-esp32s3-wamr-2.4.3-v6.zip
```

默认打包 `targets/` 下全部开发板；`--boards lckfb-s3,waveshare-35b` 可只打子集。SDK 内的契约已经更新时，省略 `--contract`。本机已有配置指定的 Windows 编译器缓存时，也可省略 `--windows-toolchain-bundle`。远程依赖使用 `--windows-toolchain-url URL --windows-toolchain-sha256 SHA256`（固定编译器包只服务单配置；多后端包需要把每个配置的原生工具预先备好）。

首次联网获取官方 Windows 嵌入式 Python 和 Python wheels，之后通过 `build/portable-cache/` 复用已锁定并校验的文件。更新 Python 依赖版本时需同步更新脚本固定版本并清理对应的 wheel 缓存，以重新生成 `resolved.json`；每个发布包保留依赖清单与校验值。

输出在 `dist/portable/`，每个主机平台一个包，文件名包含 ABI、主机与构建指纹。包内包含 `release.json`（`boards` 列表）、`toolchain-profile.json`（`profiles`：每个编译配置的后端、格式与板卡）、`dependencies.json` 和 `SHA256SUMS.txt`。三个 Windows EXE 必须随整个目录一起替换；统一入口会拒绝混用不同发布版本的工具。

Linux 包生成前会用包内工具为每块已登记的板各建一个独立项目，实际编译 WASM 与对应后端的 AOT，并验证 APP 包及 AOT 格式。Windows 包可在 Windows 上运行同一验证（同样逐板循环）：

```powershell
.\runtime\python.exe .\tools\verify_windows_release.py .
```

## Gitee 发布与下载

当前源码仓库为 `https://gitee.com/helloworldmyj/aster-sdk`。先提交并推送源码，再创建 `sdk-v*` 标签，在仓库的发行版页面使用该标签创建版本。把生成的 Windows ZIP、Linux tar.gz 和 SHA256 校验文件作为附件上传。固定 Windows 编译器 ZIP 也需要在发行版或制品库提供下载，以便新 Ubuntu 发布机器取得依赖；克隆 Git 仓库本身不会获得这个 ZIP。

应用开发者从发行版下载与电脑平台匹配的 SDK 成品（每个平台一个包，内含全部开发板），完整解压后运行 `aster install` 生成环境脚本。Windows 成品自带 Python；Linux 成品需要 Python 3.12。无需克隆 SDK 源码或自行编译工具链。接口更新后下载新 SDK、重新激活环境并重编应用，设备固件应与该 SDK 的板卡和 ABI 匹配。

## GitHub CI 与后续架构

`.github/workflows/portable-sdk.yml` 仅在 GitHub 执行，推送到 Gitee 不会触发。该流程在 Ubuntu 完成两个平台的打包。Windows runner 只下载成品、验证 EXE 和 AOT 构建，不克隆源码、不再打包。测试通过后，`sdk-v*` 标签才发布 GitHub Release；普通提交只生成可下载的 CI 制品。

使用 GitHub 自动发布时，先把固定 Windows 编译器 ZIP 上传到可访问的制品库，再配置仓库变量 `ASTER_WINDOWS_TOOLCHAIN_URL`、`ASTER_WINDOWS_TOOLCHAIN_SHA256`。旧 Windows 单文件打包流程保留为手动兼容入口。

添加 RISC-V 或其他开发板时：

1. 固件导出新板卡的契约，给出准确 CPU、AOT target、ABI、能力与协议。
2. 为该架构准备匹配固件运行时的 AOT 编译器，登记 Linux/Windows 主机二进制与 SHA256。与既有板卡共用完全相同的后端和编译参数时复用 profile，否则新增一个 profile；新 CPU 家族通常对应新后端，发布包会在 `toolchain/backends/<backend>/` 下与既有后端并列携带，`--target` 自动选择。
3. 在 `toolchains/catalog.json` 加板卡绑定。缺少主机工具链、架构不一致、同一后端挂两个 profile，或包内 wasi-sdk 版本不一致时，发布直接报错。
4. 用该板执行独立 APP 构建、设备握手、安装和实机运行，再启用对应的 CI 发布矩阵；需要不同烧录协议时扩展设备工具。

仅添加接口通常更新契约并重新封装；只有 CPU 后端、AOT 格式、运行时兼容性或编译选项改变时才重建对应工具链。当前流程只支持 x86_64 主机，其他主机架构需要增加对应运行时与原生工具配置。

## 共享串口 SDK 更新（2026-10-05）

新版固件在 shell 串口同时提供日志、命令与 ASTERFS 3 文件传输。部署握手包含 `serial=shared` 时，SDK 自动使用同一连接上传、安装、运行；LCKFB 如 `aster run --port /dev/ttyUSB0`，Windows 如 `aster run --port COM24`。旧固件继续支持显式双串口参数。独立监视器在传输、shell 或截图前关闭，完成后再打开同一端口。

此更新不改变公开 ABI 3.7.0、ASTP 包格式或 AOT 格式 6，已有 APP 不需要因此重编。此次发布使用已登记的 `lckfb-s3` 与 `waveshare-35b` 工具链配置。SDK 主机工具代码必须同步更新；便携包从新版脚本生成，不能只复制文档并沿用旧运行工具。

两种主机包现在完整携带 `docs/`、`.agents/skills/aster-app-development/`、`apps/` 示例、`assets/` 与 README，保留 Skill 到开发手册的相对链接。手册、Skill、示例与素材、README 和许可证均纳入构建指纹，包内 SHA256 清单也包含它们。发布时工具、手册和 Skill 随同一个 ZIP/tar.gz 交付，避免文档与工具混用。详细操作见 `docs/serial-console-transfer.md`。

## waveshare-35b 目标（2026-10-05）

固件契约现同时导出 `lckfb-s3` 与 `waveshare-35b`（微雪 ESP32-S3-Touch-LCD-3.5B，320×480，无 camera 能力，状态为实验性）。SDK 导入契约后重新运行一次 `bash release.sh --hosts linux-x64,windows-x64`：Windows 便携 ZIP 与 Linux 包各自携带两块板的 target，编译与烧录用 `--target` 选板。两个目标共用 `esp32s3-wamr-2.4.3-v6` 编译配置，不新增原生编译器；`tests/test_sdk.py` 对两块板分别执行独立 APP 构建。

APP 不应写死 240×320：微雪为 320×480，使用 `aster_display_get_info()` 读取实际尺寸并按返回的宽高布局。微雪 usb-debug 固件的 shell 串口使用同一套 `serial=shared` 部署路径，命令与 LCKFB 相同，只把 `--target` 换成 `waveshare-35b`；同一个便携包同时携带两块板的 target 文件，换板只需换 `--target`，不必换包。
