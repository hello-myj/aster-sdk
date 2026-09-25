# Windows APP 开发与设备工具

Windows x86-64 使用两个命令行 EXE：

- `aster-toolchain.exe`：把 APP C 源码编译为 Wasm、用 Xtensa `wamrc` 生成 AOT，打成并校验 `.ast`。
- `aster-device.exe`：扫描串口、传文件、安装 APP、烧录已构建的固件镜像、查看串口日志和 shell、截屏。

在 PowerShell 的 SDK 根目录运行 `./tools/package_windows.ps1`。若本机阻止脚本执行，
可在该次终端中运行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\package_windows.ps1`。
脚本需要 Python 3.12、
网络和至少 3 GB 临时磁盘空间；首次下载官方 wasi-sdk 24 Windows 包及 WAMR 2.4.3
Windows `wamrc.exe`，然后用 PyInstaller 在 `dist/windows/` 输出两个 EXE。
已有本机工具链可传入 `-WasiSdk C:\path\to\wasi-sdk` 和
`-Wamrc C:\path\to\wamrc.exe`。两个官方工具链下载包都校验固定 SHA-256。
`.github/workflows/windows-tools.yml` 在 Windows runner 上打包，并用 `apps/hello`
验证 AOT 构建与包校验。Windows EXE 必须在 Windows 上构建和验证。

在 APP 项目所在目录运行示例：

```powershell
aster-toolchain.exe build C:\work\my-app --target lckfb-s3 --aot
aster-toolchain.exe verify dist\lckfb-s3\my.app.ast --target lckfb-s3
aster-device.exe build C:\work\my-app --toolchain C:\tools\aster-toolchain.exe --target lckfb-s3
aster-device.exe ports
aster-device.exe probe --target lckfb-s3
aster-device.exe build-deploy C:\work\my-app --toolchain C:\tools\aster-toolchain.exe --port COM5 --console COM6 --run
aster-device.exe push C:\work\sample.txt /SD:/sample.txt --port COM5
aster-device.exe shell --console COM6 aster app list
aster-device.exe debug --console COM6 --log C:\work\aster.log
aster-device.exe screenshot C:\work\screen.png --console COM6
aster-device.exe flash C:\work\merged-firmware.bin --port COM6 --address 0x0
```

`build-deploy` 先本机编译，再经 USB 串口传输并安装 APP。没有显式指定数据串口时，
工具枚举 Windows COM 口并用 `ASTERPING` 识别文件服务；控制台也可自动探测。
数据串口和控制台通常是两个不同 COM 口。固件烧录使用 esptool，必须显式提供
烧录串口；`flash` 默认按 ESP32-S3 的合并镜像从 `0x0` 写入，可用 `--address`
覆盖镜像偏移。`debug` 监视串口日志，也可输入 shell 命令；`shell` 发送单条控制台命令。
目前不包含源码级 GDB 调试。

截屏读取固件 `aster ui screenshot` 输出，逐块验证长度与 CRC，然后写出 PNG；
串口数据损坏时自动重试。若设备没有启用截屏服务，命令会报错。
