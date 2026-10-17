# 一次下载完整 APP 开发环境

SDK 是独立的 APP 开发环境。开发 APP 无需安装 Zephyr、ESP-IDF 或下载 Espressif 驱动库，也不依赖固件源码目录。固件维护者构建整机镜像时才使用这些依赖。

从 [SDK Release](https://github.com/hello-myj/aster-sdk/releases/tag/sdk-v3.7.0-20261015.1) 下载自己平台的完整 SDK 包：Windows ZIP 或 Linux tar.gz。每个包同时包含：

- APP 开发 SDK：CLI、WASM/Xtensa AOT 编译器、开发手册、AI Skill、示例及素材。
- `firmware/`：LCKFB 与微雪两款板的独立固件镜像、构建身份和 SD 字体资源。
- `simulator/`：对应主机的便携模拟器与 Node 运行时，无需另装 Node。
- `development-kit.json` 和 `contract/release-family.json`：这组 SDK、固件和模拟器的 tag、完整提交号、ABI、AOT 格式、镜像与组合载荷的 SHA256。

本组版本为 SDK `sdk-v3.7.0-20261015.1`、固件 `firmware-v0.1.0-20261015`、模拟器 `sim-v0.2.0-20261015`，公开 ABI 为 3.7.0，Xtensa AOT 格式为 6。版本以包内 JSON 为准，SDK Release 汇集全部成品，不需要到其他仓库寻找配套版本。其他 ABI 相同的组合不自动视为已验收组合。

模拟器实现这组公开 APP 接口的主机模型：执行 WASM，模拟图形、输入、存储和部分外设，忽略 AOT；不模拟 ESP32 CPU、Zephyr 调度、QSPI/I2C、电气故障或固件 watchdog 的完整复位。模拟通过后仍需在所选开发板镜像上验证 AOT 与硬件行为。

## 使用

完整解压后运行 `aster install` 并激活生成的环境脚本。Windows 自带 Python；Linux 需要 Python 3.12。工程构建仍使用 CPU 目标 `xtensa`，换板不改变 SDK 目标。

```powershell
# Windows：从完整 SDK 解压目录启动
.\aster.exe init .\my-app --template hello --id com.example.hello --target xtensa
.\aster.exe build .\my-app --aot
.\simulator\aster-sim.exe .\my-app\dist\xtensa\com.example.hello.ast --resolution 240x320
# 微雪竖屏布局改用 --resolution 320x480
# 需要更换设备固件时，选择正确板型镜像，并替换实际串口
.\aster.exe flash .\firmware\aster-firmware-0.1.0-lckfb-s3.bin --port COM24
.\aster.exe run .\my-app --port COM24
```

Linux 使用 `./aster` 和 `./simulator/aster-sim`，串口如 `/dev/ttyUSB0`。将 `firmware/sdcard/` 的内容复制到 FAT 格式 SD 卡根目录，得到 `aster/fonts/`；保留现有应用和存档。两款板镜像不能混刷。本组内核堆96 KiB；微雪保持RGB565、40 MHz、双8 KiB内部预拷贝，IMU仍禁用。LCKFB所含镜像通过短时自动实机回归，微雪96 KiB镜像尚未实机补测；以包内build identity的hardware_test和scope为准，不能把源码此前的硬件结果解释成每个二进制均已实机验收。

## 发布维护

固件和模拟器各自保持独立源码 tag；SDK 维护 `contract/release-family.json` 固定配套成品。便携打包只消费已生成的组合载荷，不编译固件或模拟器，不读取其内部源码。载荷 SHA256、板型镜像哈希或 ABI 不符会阻止打包。完整 SDK 包的 SHA256 清单覆盖固件、模拟器和 SD 资源。

本地载荷已有时执行 `bash release.sh --hosts linux-x64,windows-x64 --development-kit-dir /path/to/pinned-payloads`；新发布环境可从清单的固定 SDK Release 地址下载已发布载荷。`aster-development-kit-*` 附件只供发布维护者复用，普通开发者下载 `aster-sdk-*` 完整包即可。

## 2026-10-15版本说明

本组包含G4–G9期间的图形/Scene、字体和资源路径改进，以及Wi-Fi设置铺满与关闭修复、launcher顶部下拉打开调音台/底部上拉收回。12px字库保留原尺寸并采用已验收的原生hinting配方；SD资源与固件一起固定。两个主机包均包含G8/G9对应模拟器，仍执行WASM而不是仿真ESP32硬件。

APP ABI仍为3.7.0、Xtensa AOT格式仍为6。TCP/UDP、HTTP/HTTPS、MQTT等新APP网络接口尚未实现；网络实施方案是固件仓库的后续计划，不属于本版SDK可调用API。物理SPI/GDMA故障恢复与微雪96 KiB镜像实机验收仍有待办，不因本组发布自动标记完成。

## ABI 3.11 工作区与 N4 本地候选

上文3.7版本说明仅描述已发布组合。当前工作区已增加TCP/UDP、HTTP/HTTPS、流式音频和MQTT/MQTTS；完整声明见开发手册第15–18节，旧3.7镜像不能运行引用这些新导入的APP。N4使用明确标识为未发布的本地3.11候选复核两板镜像、系统资源和宿主模拟器，候选未替换任何已发布tag或下载附件。硬件通过状态与源码是否包含未提交改动，以候选包内身份文件为准；在正式发布前不能将候选称为新的Git Release。
