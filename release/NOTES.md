# Aster SDK 4.5.0

本版提供 Windows x64 和 Linux x64 的独立 APP 开发包，支持 Xtensa 与 RISC-V AOT。相较 4.3，新增可选系统媒体接口和蓝牙设备枚举，`aster verify` 能直接读取安装包的架构、所需能力及板型。通用应用按架构编译；专用应用可在 `app.toml` 的 `[app].supported_boards` 声明允许的设备。开发手册、示例和 SKILL 已同步。

## 从一个应用开始

1. Windows 完整解压 ZIP，在开发包目录运行 `.\aster.exe install` 并按提示加载环境。Linux x64 完整解压 tar.gz，准备 Python 3.12，运行 `./aster install` 和 `source env.sh`。
2. 创建：`aster init my-app --template hello --id com.example.hello --target riscv`。立创 ESP32-S3 实战派使用 `--target xtensa`。
3. 编译：`aster build my-app --aot`。验证：`aster verify my-app/dist/riscv/com.example.hello.ast`。Xtensa 的产物位于 `dist/xtensa/`。
4. 实机部署：`aster deploy my-app/dist/riscv/com.example.hello.ast --target riscv --port COM11 --run`。替换为实际架构和串口；Windows 桌面也可用 `aster sim --profile osptek-p4 --app my-app` 调试普通应用。

## 配套组件

执行 `aster firmware list` 查看板型。`aster firmware download --board fakepod-s31 --output images` 从固件 Release 获取并校验镜像。换板时选择对应镜像。需要给空白卡离线初始化时，下载固件 Release 中配套的 SD 卡 ZIP，按说明复制到卡根目录。

[固件 4.5.0 及离线资源](https://github.com/hello-myj/aster-firmware/releases/tag/firmware-v4.5.0) · [离线复制说明](https://github.com/hello-myj/aster-firmware/blob/firmware-v4.5.0/docs/offline-sd.zh-CN.md) · [Windows 桌面模拟器 1.1.0](https://github.com/hello-myj/aster-sim/releases/tag/sim-v1.1.0)
