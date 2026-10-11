> 此文是 2026-10-05 的历史发布交接记录，其中按开发板选择目标的命令和包清单已过期。当前使用 `--target xtensa`，发布步骤以 [发布指南](release.md) 为准；历史标签与产物记录保留。

# 给 DeepSeek 的 Aster SDK Gitee Release 发布交接

编写日期：2026-10-05。任务是将新版 SDK 发布到 Gitee 的 Git Release，供用户下载 Windows/Linux 开发包。本文由当前代理准备；当前代理没有执行远端提交、推送、建标签或发布。请在能够访问 Gitee 的环境执行。

## 1. 任务范围与权威工作区

仓库：https://gitee.com/helloworldmyj/aster-sdk

使用较新的 Windows 工作区作为发布源码：

- Windows：`E:\个人项目资料\aster\aster-sdk`
- WSL：`/mnt/e/个人项目资料/aster/aster-sdk`
- 本文的 Windows 副本：上述目录下 `docs/gitee-release-deepseek.md`。

`/home/myj/zephyrproject/aster/aster-sdk` 是较旧的 WSL SDK 工作区。虽然同步了共享串口功能，但发布流程较旧，不要将它整个覆盖到 Windows 工作区。本文也放了一份在该目录，方便交接；实际发布仍使用 Windows 工作区。

本次发布目标为 `lckfb-s3` 与 `waveshare-35b`（微雪 ESP32-S3-Touch-LCD-3.5B，实验性支持），SDK ABI 为 `3.7.0`，主机为 `windows-x64`、`linux-x64`，AOT 格式为 6；**每个主机平台一个包，包内携带全部开发板，共两个包**（此前按板拆分四包的设计已作废）。不要仅为主机工具更新修改 ABI。`sdk-v3.7.0-r1`、`sdk-v3.7.0-r2`、`sdk-v3.7.0-r3` 标签已存在并已推送（r3 指向 `336abd4` 的中间态提交，四包方案，未建 Release；不移动、不删除；r1/r2 指向更早的 lckfb-only 提交）。本次发布使用新提交上的递增标签 `sdk-v3.7.0-r4`，但必须先查远端是否已使用；如果已存在，不覆盖，另定递增标签。

当前工作区存在大量未提交改动，其中包括用户和其他代理的改动及换行差异。请审查实际差异，保留有意义的改动，不要直接 `git add .`、丢弃文件、重置工作区或强制推送。调试脚本、预览、构建缓存和发布大包不应顺手提交。本文没有授权修改固件仓库或刷机。

## 2. 现有包的状态：仅作参考，不直接发布

此前验证包在未提交的源码上构建，其 `release.json` 记录的是旧 HEAD：

```text
commit: 415bc694030c36ee77b0fcf3763e46dbcf3957a5
build_id: 55526d503e4d3ebc97ee2c74db11c00cef33e402dd6b0c94ad4b17565bd62f44
```

它们位于 Windows 工作区的 `dist/portable/`，备份在旧 WSL SDK 的 `dist/releases/`：

```text
aster-sdk-3.7.0-windows-x64-lckfb-s3-55526d503e4d.zip
大小：86187935 字节
SHA256：ef8ea9272c8fdbe95fb75e82205f0ffdc0985f98c08cdde4cbb12a1b3931c1b2

aster-sdk-3.7.0-linux-x64-lckfb-s3-55526d503e4d.tar.gz
大小：79853760 字节
SHA256：1e58ed10754b2b594086144977d05e96066e2d315bcdd9d6d5eb51cdc2efcf40
```

**先提交最终发布源码，再重新构建两个包（每个主机平台一个），标签指向该提交。** 不要把旧包挂到新提交的标签，也不要手工修改 ZIP 内的提交信息。`dist/portable/` 中已有的更早 lckfb-only 包（含 r2 轮产物）与 r3 轮按板拆分的四个包（build_id `8cb06c82…`）同样不再用于本次发布。

文档、Skill、示例和素材均参与 `build_id` 计算；加入本文已经改变源码指纹，因此最终包名应以重新构建的输出为准，不再照抄上面的 `55526d503e4d` 文件名。构建期间不要改源码、手册、Skill 或示例。

## 3. 发布前检查与提交

在 WSL/Ubuntu 执行只读检查：

```bash
cd '/mnt/e/个人项目资料/aster/aster-sdk'
git status --short
git branch --show-current
git remote -v
git diff --stat
git diff --ignore-space-at-eol --stat
git ls-remote --heads --tags origin
```

确认 origin 是上面的 SDK 仓库，核对当前分支与远端关系。检查共享串口工具、文档、发布脚本及测试的完整改动；逐项暂存拟发布文件，检查 `git diff --cached` 后创建发布准备提交。如果存在归属不明的实质改动，先向用户确认该改动范围，不凭空删掉。

提交后记录完整 `git rev-parse HEAD`。发布内容应来自该提交：涉及发布指纹的未提交文件必须处理完，不可带着未提交源码打正式包。不要通过 `ASTER_RELEASE_COMMIT` 环境变量伪装提交号；若环境已设置该变量，构建前清除。不要在本轮临时导入新的固件契约；如确需导入，应在提交之前完成。

## 4. 从同一提交重新打包

发布器必须在 Linux x86_64、Python 3.12 下执行。Windows 包由 Ubuntu/WSL 交叉构建，Windows 用于验证成品。当前环境已验证的解释器是：

```text
/home/myj/zephyrproject/aster/aster-sdk/.venv-linux/bin/python
Python 3.12.13
```

先确认该路径存在和版本正确；换环境时可用 Python 3.12 建虚拟环境并安装 `requirements.txt`。还需要 `x86_64-w64-mingw32-gcc`（Ubuntu 包 `gcc-mingw-w64-x86-64`）。

已有固定依赖可复用：

- `toolchain/wasi-sdk-24.0-x86_64-linux`、`toolchain/wamrc`
- `build/windows-downloads/wasi-sdk-app`
- `build/windows-downloads/wamrc-xtensa/wamrc.exe`
- `build/portable-cache/` 中的嵌入式 Python 和依赖缓存

不要随意升级编译器或把 Linux 二进制当 Windows 工具。新机器缺少缓存时，按 `docs/release.md` 与 `tools/toolchain_bundle.py` 准备已锁定的 Windows 工具链包；远程工具链必须核对 SHA256。

在本机执行：

```bash
cd '/mnt/e/个人项目资料/aster/aster-sdk'
unset ASTER_RELEASE_COMMIT
export ASTER_RELEASE_PYTHON=/home/myj/zephyrproject/aster/aster-sdk/.venv-linux/bin/python
"$ASTER_RELEASE_PYTHON" --version
"$ASTER_RELEASE_PYTHON" -m unittest discover -s tests -v
bash release.sh --hosts linux-x64,windows-x64
```

默认打包 `targets/` 下全部开发板（本次为两块）；`--boards` 只在需要子集时使用。更换解释器路径时相应调整环境变量。记录本次输出的两个 `Packaged:` 路径（一个 Windows ZIP 与一个 Linux tar.gz），产物在 `dist/portable/`。目录中可能有旧包或 `.partial` 文件，仅选本次成功生成的两个完整包。

分别解压到全新的临时目录，核对两个包：

1. `release.json.commit` 均等于发布准备提交的完整 HEAD；两个包 `build_id` 相同，ABI 为 3.7.0；`release.json.boards` 列出两块板。
2. 包内 `targets/` 同时含 `lckfb-s3.json` 与 `waveshare-35b.json`；`toolchain-profile.json.profiles` 记录同一 Xtensa / AOT 格式 6 配置（`esp32s3-wamr-2.4.3-v6`）覆盖两块板；单配置包的工具链保持平铺 `toolchain/wamrc[.exe]` 布局。
3. 包含完整工具、运行时、工具链、文档、Skill、示例和素材，以及内部 `SHA256SUMS.txt`。
4. 内部 SHA256 清单校验通过，三个工具报告的构建身份一致。

Windows 全新解压后，在 `aster-sdk` 根目录运行 PowerShell：

```powershell
.\runtime\python.exe .\tools\verify_windows_release.py .
```

验证程序检查内部清单、工具身份、依赖、环境安装，以及在 SDK 外部为每块已登记板创建应用、编译 AOT、验证 APP 包。Windows 三个 EXE 依赖整个便携目录，不能只测试或分发三个 EXE。Linux 发布器已包含独立应用构建验证（逐板循环）；Linux 使用者需要 Python 3.12。

给本次两个包各生成一个外部 `.sha256` 文件，下面命令中的文件名替换为本次真实输出名：

```bash
cd '/mnt/e/个人项目资料/aster/aster-sdk/dist/portable'
sha256sum '本次 Windows包.zip' > '本次 Windows包.zip.sha256'
sha256sum '本次 Linux包.tar.gz' > '本次 Linux包.tar.gz.sha256'
sha256sum -c '本次 Windows包.zip.sha256'
sha256sum -c '本次 Linux包.tar.gz.sha256'
```

校验文件只记录文件名，便于下载后在同一目录检查。不要将实际包名中的中文占位符原样执行。

## 5. 推送源码及标签

只有打包、校验成功后，才给记录的发布提交创建带说明的标签。示例标签仅在确认远端未使用时有效：

```bash
cd '/mnt/e/个人项目资料/aster/aster-sdk'
git tag -a sdk-v3.7.0-r4 <发布提交完整SHA> -m 'Aster SDK 3.7.0 r4: one portable package per host with every board target'
git push origin <核实后的当前发布分支>
git push origin refs/tags/sdk-v3.7.0-r4
git ls-remote origin refs/tags/sdk-v3.7.0-r4 'refs/tags/sdk-v3.7.0-r4^{}'
```

尖括号内容必须替换成真实值。核对远端带说明标签解引用后的提交等于发布提交。已存在的 `sdk-v3.7.0-r1`/`r2`/`r3` 标签保持原样，不移动、不删除。若远端分支有新提交，正常处理并重新确定发布快照，必要时重新打包；不使用 `--force` 或推送所有标签。包本身作为 Release 附件，不放进 Git 源码提交。

推送 Gitee 标签不会运行 `.github/workflows/portable-sdk.yml`，也不会自动上传 SDK 包。不要用 GitHub 的 `gh release create` 代替 Gitee 发布。

## 6. 创建 Gitee Release 并上传附件

最稳妥的可执行路径是 Gitee 网页：进入 SDK 仓库的“发行版 / Releases”，选择已推送的标签，填写标题和下节说明，上传本次两个成品（Windows ZIP、Linux tar.gz）和两个 `.sha256` 文件（合并为**一个** Release，共四个附件），然后发布。`sdk-v3.7.0-r2`、`sdk-v3.7.0-r3` 标签保留但不建 Release。不要将网页自动生成的“源码 ZIP”当成可直接开发的便携 SDK。

若使用 API，先在可访问 Gitee 的环境核对官方文档的当前参数与权限。官方 SDK 文档列出的 v5 接口包括：

- `GET /repos/{owner}/{repo}/releases/tags/{tag}`：查该标签已有的发行版。
- `POST /repos/{owner}/{repo}/releases`：创建发行版。
- `POST /repos/{owner}/{repo}/releases/{release_id}/attach_files`：上传附件。
- `GET /repos/{owner}/{repo}/releases/{release_id}/attach_files`：列出附件。
- `GET /repos/{owner}/{repo}/releases/{release_id}/attach_files/{attach_file_id}/download`：下载附件。

基础 URL 是 `https://gitee.com/api/v5`，owner 是 `helloworldmyj`，repo 是 `aster-sdk`。参考 [Gitee 官方 SDK 的 RepositoriesApi 文档](https://gitee.com/sdk/gitee5j/blob/main/docs/RepositoriesApi.md)。当前代理无法取到该页面完整内容，尚未在线确认上传字段、账号附件大小限制或当前返回结构，执行者必须现场核对，不猜 multipart 字段。API 不方便时直接使用网页上传，无需为本次发布新建 CLI。

Git SSH 凭据与 API Access Token 不一定相同。Token 由用户在执行环境私下提供，通过环境变量或安全输入读取；不要写入本文、源码、Release 正文、命令 URL 或日志。请求重试前先检查已有 Release 和附件，避免重复创建。已有同名附件但内容不同，不直接覆盖或删除；报告冲突并确定新版本处理方式。

必须上传以下四个文件：Windows ZIP、Linux tar.gz，以及每个包的 SHA256 文件（每个平台一个包，内含全部开发板）。当前参考包约 82 MiB/76 MiB，先核对账号附件限制（四个附件的总量）；若超限，报告实际错误并与用户确定制品存储方式，不声称发布已完成。

固定 Windows 编译器依赖 ZIP 如需供后续发布者使用，可另附经验证的工具链包及 SHA256，并在说明中标注“发布机器依赖，普通 APP 开发者无需另下”。该依赖包应遵循现有 `docs/release.md` 的获取方式，不把未验证缓存随意打包上传。

## 7. 可直接填写的发行说明

将下面占位内容替换成最终真实值：

```markdown
Aster SDK 3.7.0 r4

适用于两块 ESP32-S3 开发板：立创实战派（lckfb-s3）与微雪 ESP32-S3-Touch-LCD-3.5B（waveshare-35b，实验性支持）。Windows x64 与 Linux x64 各一个便携 SDK，包内携带全部开发板，构建与烧录用 --target 选板，共两个包。
源码提交：<完整提交SHA>
构建指纹：<完整build_id>

更新内容：
- 便携包改为每个主机平台一个：包内同时携带全部已登记开发板（lckfb-s3、waveshare-35b），编译与部署用 --target 选择开发板，编译器自动切换；不再按板拆分下载。
- 新增 waveshare-35b 发布目标：320×480、无 camera 能力、状态 experimental；由固件契约导出。
- 两块板共用 Xtensa / AOT 格式 6 的同一编译器配置（esp32s3-wamr-2.4.3-v6），不新增原生编译器。
- 继承 r2：支持新版固件的共享串口——日志、shell 命令和文件传输使用同一物理串口，部署工具识别 serial=shared 并在同一连接完成上传、安装与运行；保留旧固件的显式双串口兼容方式。
- 发布包完整携带开发手册、AI Skill、示例应用、素材与工具链，文档及示例纳入构建指纹和校验清单。

兼容性：公开 ABI 仍为 3.7.0，ASTP 应用包和 AOT 格式 6 不变；已有 APP 不需要重编。
APP 的 target_id 必须与 --target 及设备一致；微雪目标上 APP 需要通过 aster_display_get_info() 读取实际分辨率。
单串口能力需要匹配的新版固件；更新 SDK 不会自动更新固件。

使用方法：
- 下载与电脑平台匹配的包：Windows 用 ZIP，Linux 用 tar.gz，一个包应付全部开发板。
- Windows ZIP 完整解压，保留整个目录；自带 Python。Linux tar.gz 完整解压；需要系统 Python 3.12。
- 按包内手册运行 aster install 并激活环境；构建、部署时用 --target 选择开发板（lckfb-s3 / waveshare-35b）。
- 独立串口监视器在传输、shell 或截图前关闭，操作结束后可重新打开。
- 校验下载文件：在附件目录运行 sha256sum -c <对应.sha256文件>；Windows 可用 Get-FileHash -Algorithm SHA256 比对。

验证：<填写本次重新构建的单元测试、Linux应用构建验证、Windows成品验证、LCKFB 与微雪实机结果>
下载附件：<本次两个包及两个校验文件的真实名称>
```

不要把模板中的测试结果写成未执行的成功，也不要声称解决了所有 IMU 硬件问题。

## 8. 已有验证证据与最终验收

2026-10-05 的 r2 参考构建完成了 48 项 SDK 单元测试，Linux 独立应用构建、Windows 原生完整包验证，以及 LCKFB 的真实共享串口探测、4101 字节二进制传输 CRC 和 IMU shell 命令验证（Windows 当时端口为 COM24；端口号会随连接改变，不作为发布硬编码配置）。r3/r4 轮在源码层新增 waveshare-35b 目标并把打包改为每平台一包：单元测试通过，Linux 包内应用构建与 Windows 成品验证对两块板分别执行；微雪实机部署验证 <填写结果>（用本次 Linux 便携包进行）。

记录在：

```text
/home/myj/zephyrproject/aster/diagnostics/sdk-serial-release-20261005/
```

这些证明参考版本经过验证；新提交重新打包后仍需完成上面的成品校验。测试失败应修复后重新提交、重建，避免标签和包脱节。没有设备时可以说明未重复实机验证，不把构建通过写成设备测试通过。

发布后，从 Gitee 实际下载四个附件到新的空目录，按外部 SHA256 验证两个包；私有仓库按授权方式下载。确认 Release 链接可用、标签与提交一致、附件名与本次构建一致。不能只检查 HTTP 上传成功就宣称交付完成。

最终向用户报告：

1. Gitee Release URL、标签名与完整源码提交 SHA。
2. 两个包共用的完整 build_id。
3. 四个实际附件名，两个包的大小与 SHA256。
4. 本次构建和成品验证结果、远端下载校验结果。
5. 未完成项目及原因（如果存在）。

本文只是交接文档；截至写入时，尚未创建本次正式发布提交、远端标签或 Gitee Release。
