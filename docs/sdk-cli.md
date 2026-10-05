# 统一 SDK 命令（Windows / Linux）

SDK 提供 `aster` 命令。项目只需要 `app.toml`、源代码和素材，不需要项目专用构建脚本。

## Linux 安装与环境

当前便携包支持 x86_64 Linux、Python 3.12，包含 Python 依赖及 wasi-sdk / Xtensa wamrc。完整解压后运行 `./aster install`，再 `source env.sh`。以下 `./bin/aster` / venv 安装步骤针对 SDK 源码仓库（Python 3.11+）。Python 版本太旧时，先通过发行版包管理器安装 Python 3.11+ 和对应 venv 包（例如 Ubuntu 24.04 的 `python3 python3-venv`）。

```bash
cd /path/to/aster-sdk
./bin/aster install --add-path
source ./env.sh
aster doctor
```

`install` 把 pyserial 和 cryptography 安装到 SDK 的 `.venv-linux`，不修改系统 Python。`--flash` 额外安装 esptool，用于刷固件。首次安装需要联网访问 Python 包仓库。若 Windows Git 检出把 Linux 工具链符号链接变成小文本文件，安装时会恢复这些链接。

`--add-path` 在当前用户的 `~/.bashrc` 或 `~/.zshrc` 中写入一个可重复更新的 Aster 区块；可用 `--profile /path/to/profile` 指定其他文件。`source env.sh` 立即在当前终端生效；以后交互终端自动生效。登录 shell、CI 可直接 source 该文件。

## Windows 安装与环境

完整解压到固定目录，保留三个 EXE 和 `runtime/`、`tools/`、`toolchain/` 等所有子目录。便携包包含 Python 依赖、Wasm 编译器和 Xtensa AOT 编译器，不需要安装 Python、WSL 或 Visual Studio。

```powershell
cd E:\AsterTools
.\aster.exe install --add-path
. .\env.ps1
aster doctor
```

`--add-path` 更新当前用户 PATH，不要求管理员权限；新开的终端生效。PowerShell 当前终端使用 `. .\env.ps1`；CMD 使用 `call env.cmd`。移动安装目录后，请在新位置重新执行安装并删除旧 PATH 项。

开发 SDK 源码时，也可使用 `bin\aster.cmd`（优先调用 `dist\windows\aster.exe`，没有 EXE 时调用 Python 3.11+）；源码依赖环境使用 `.venv-windows`，与 Linux 的 venv 分开。源码方式需要本机 Windows wasi-sdk 和 Xtensa wamrc，发布 EXE 已包含这些工具。原生 EXE 打包入口是 `tools/package_windows.ps1`。

## 创建、编译、运行

安装并激活后，两个平台的项目命令完全相同：

```text
aster templates
aster init my-game --template swipe-game --id com.example.mygame --name "My Game" --target lckfb-s3
cd my-game
aster build
aster run
```

- `templates`：列出内置模板。目前有 `hello`（最小生命周期示例）和 `swipe-game`（滑动控制的落块游戏，含图标和四种音效）。
- `init <目录>`：创建项目，拒绝覆盖已有目录。目录可以在 SDK 之外。
- `--template`：选择模板，默认 `hello`。
- `--id`：应用唯一标识，建议使用 `com.example.game`；省略时从目录名生成 `app.<目录名>`。更改名称不必更改 ID。
- `--name`：安装到设备后显示的应用名称，省略时使用目录名。
- `--target`：目标板，保存到项目 `app.toml` 的 `[sdk] target` 中。本发布批次为 `lckfb-s3` 与 `waveshare-35b`（同一个便携包内含两块板，按 `--target` 选择），以 `aster targets` 输出为准。
- `build [项目目录]`：编译并验证 `.ast` 包，省略目录时使用当前目录。模板默认 AOT；`--no-aot` 临时关闭，`--aot` 强制开启。
- `run [项目目录]`：编译、传输、安装并启动应用。`--no-build` 使用已有包。
- `--port`：文件传输端口。新固件同时在 shell 串口提供文件服务，探测返回 `serial=shared` 时，安装和运行自动使用同一个端口，无需指定 `--console`。例如 `aster run --port COM24` 或 `aster run --port /dev/ttyUSB0`。`--console` 仍可覆盖选择，以兼容旧固件的双串口模式。监视器与上传工具不能同时独立打开、读取同一个串口：上传前关闭监视器，完成后重新打开。

Linux 串口需要当前用户具有读写权限；通常由设备所属的 dialout/uucp 组控制，按发行版配置后重新登录。`install` 不自动修改用户组或使用 sudo。

目标板优先级：命令行 `--target` > 项目 `[sdk].target` > `ASTER_TARGET` > SDK 仅有的一个目标板。输出固定在项目的 `build/<target>/` 和 `dist/<target>/<id>.ast`，不依赖当前 SDK 所在位置。新增素材或多个 C 文件在 `app.toml` 的 `assets` / `sources` 中声明。

## 其他命令

```text
aster targets
aster doctor
aster verify dist/lckfb-s3/com.example.mygame.ast
aster deploy dist/lckfb-s3/com.example.mygame.ast --run
aster ports
aster probe --port COM24
aster debug --console COM24
aster screenshot screen.png --console COM24
aster shell --console COM24 aster sys info
```

`doctor` 检查依赖与工具链位置；`verify` 离线检查包；`deploy` 只传输和安装现成包，`--run` 安装后启动。`debug` 查看串口日志，`screenshot` 保存实机 PNG。设备子命令的其他参数通过 `aster <命令> --help` 查询。

`aster env --shell bash` / `powershell` / `cmd` 输出激活当前 SDK 的命令，`aster source` 是别名。例如 Linux `eval "$(aster env --shell bash)"`，PowerShell `aster env --shell powershell | Out-String | Invoke-Expression`。子进程不能直接修改父终端环境，因此要 source/执行输出的环境脚本。

保留旧命令 `aster new`、`aster build --all --target lckfb-s3`，底层 `tools/build.py` 和原有两种 Windows EXE 也可以继续使用。`aster build --clean` 删除当前项目的整个 build/dist 目录（项目构建产物）；不要在这些目录保存源码。

## 本次验证

- Linux：独立 venv 安装、source 激活、hello / swipe-game 模板在 SDK 外真实编译并验证 Xtensa AOT 包。
- Windows：原生 EXE 安装、用户 PATH、PowerShell 5 中文路径激活、SDK 外带空格的游戏项目创建及 AOT 编译验证。
- 早期双串口固件：COM22 数据口 / COM24 控制台完成 APP 传输和启动。
- 2026-10-05 LCKFB 新固件：同一个 `/dev/ttyUSB0` 完成上传、安装、启动、shell 和截图；错误 CRC 拒绝及断点续传通过。微雪共享 USB 模式使用同一接口。
- SDK 回归测试：49 项通过（含 lckfb-s3 与 waveshare-35b 双板构建用例）。
