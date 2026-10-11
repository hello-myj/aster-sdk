> 日常开发推荐统一 `aster.exe`：安装、PATH、模板创建和编译运行见 [跨平台命令指南](sdk-cli.md)。下面的两个底层 EXE 仍兼容。

## 推荐发布流程

现在推荐由 Ubuntu 统一生成 Windows 和 Linux 便携 SDK，Windows 保留 EXE 启动入口。固件契约同步、CPU 平台工具链选择、一键发布和 CI 配置见 [发布指南](release.md)。

下文保留旧的 Windows 单文件 EXE 构建方式，供兼容用途；普通 SDK 更新无需使用这条路径。

# Windows APP 开发与设备工具

Windows x86-64 使用两个命令行 EXE：

- `aster-toolchain.exe`：把 APP C 源码编译为 Wasm、用 Xtensa `wamrc` 生成 AOT，打成并校验 `.ast`。
- `aster-device.exe`：扫描串口、传文件、安装 APP、烧录已构建的固件镜像、查看串口日志和 shell、截屏。

在 PowerShell 的 SDK 根目录运行 `./tools/package_windows.ps1`。若本机阻止脚本执行，
可在该次终端中运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\package_windows.ps1`。
脚本需要 Python 3.12 或 3.13、
网络、Visual Studio 2022 的“使用 C++ 的桌面开发”组件、CMake、Git，以及至少 20 GB 临时磁盘空间；首次下载官方 wasi-sdk 24 Windows 包，
并从固定提交构建 WAMR 2.4.3（AOT 格式调至固件所需 v6）与包含 Xtensa 后端的 LLVM 18，
然后用 PyInstaller 在 `dist/windows/` 输出两个 EXE。首次 LLVM 构建可能需要较长时间，后续会复用 `build/windows-downloads/` 的构建缓存。
已有本机工具链可传入 `-WasiSdk C:\path\to\wasi-sdk` 和
`-Wamrc C:\path\to\wamrc.exe`。自备的 `wamrc.exe` 也必须能编译 Xtensa；打包脚本会用真实 Wasm 进行 AOT 冒烟测试，并核对输出的格式版本为 v6。wasi-sdk 下载包校验固定 SHA-256。
`.github/workflows/windows-tools.yml` 在 Windows runner 上打包，并用 `apps/hello`
验证 AOT 构建与包校验。Windows EXE 必须在 Windows 上构建和验证；运行这些 EXE 的用户无需安装 WSL、Visual Studio 或 LLVM。

在 APP 项目所在目录运行示例：

```powershell
aster-toolchain.exe build C:\work\my-app --target xtensa --aot
aster-toolchain.exe verify dist\xtensa\my.app.ast --target xtensa
aster-device.exe build C:\work\my-app --toolchain C:\tools\aster-toolchain.exe --target xtensa
aster-device.exe ports
aster-device.exe probe --target xtensa
aster-device.exe build-deploy C:\work\my-app --toolchain C:\tools\aster-toolchain.exe --port COM6 --run
aster-device.exe push C:\work\sample.txt /SD:/sample.txt --port COM6
aster-device.exe shell --console COM6 aster app list
aster-device.exe debug --console COM6 --log C:\work\aster.log
aster-device.exe screenshot C:\work\screen.png --console COM6
aster-device.exe flash C:\work\merged-firmware.bin --port COM6 --address 0x0
```

`build-deploy` 先本机编译，再经 USB 串口传输并安装 APP。没有显式指定数据串口时，
工具枚举 Windows COM 口并用 `ASTERPING` 识别文件服务；控制台也可自动探测。
新固件探测回复包含 `serial=shared`，同一个 COM 口即可传包、安装和运行，部署工具复用同一连接；旧固件仍可用 `--port COM5 --console COM6`。独立监视器应在传包、shell 或截图操作前关闭。CH340 设备工具已调整 DTR/RTS，避免打开串口的控制线切换触发重启。固件烧录使用 esptool，必须显式提供
烧录串口；`flash` 默认按 ESP32-S3 的合并镜像从 `0x0` 写入，可用 `--address`
覆盖镜像偏移。`debug` 监视串口日志，也可输入 shell 命令；`shell` 发送单条控制台命令。
目前不包含源码级 GDB 调试。

截屏读取固件 `aster ui screenshot` 输出，逐块验证长度与 CRC，然后写出 PNG；
串口数据损坏时自动重试。若设备没有启用截屏服务，命令会报错。
