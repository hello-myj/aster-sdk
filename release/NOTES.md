# Aster SDK 4.3.0

Windows x64 与 Linux x64 APP 开发包，包含编译器、命令行工具、手册、示例和开发 Skill。支持 ESP32-S3 Xtensa 与 ESP32-P4 RV32 ILP32F AOT。

- 使用 `aster firmware list/download/flash` 获取并校验配套固件。
- 使用 `aster sim --profile p4 --app 工程目录` 启动桌面调试；lckfb 使用对应配置，支持外部资源。
- 使用 `aster deploy 安装包.ast --target riscv --port COM11 --run` 串口安装。设备显示接收、安装进度与结果。

配套组件：[固件 4.3.0](https://github.com/hello-myj/aster-firmware/releases/tag/firmware-v4.3.0)、[桌面模拟器 1.0.0](https://github.com/hello-myj/aster-sim/releases/tag/sim-v1.0.0)。组件版本与 SHA256 固定在开发包中，按需下载。

Windows 包自带 Python；Linux 包需要 Python 3.12。已验证两个主机包的独立工程编译、AOT 格式与安装包校验，Windows 生成的 P4 AOT 已在实机运行通过。
