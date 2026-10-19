# Aster SDK

独立 APP 开发仓库。应用只包含 `<aster/aster.h>` 或使用 `lib/aster/` 的图形、文字助手，通过固件公开的 `aster_*` 接口调用系统功能。无需包含 Zephyr、芯片 SDK、LVGL、驱动或固件内部头文件。

```text
include/aster/       固定版本的公开接口副本
contract/           API 描述、运行时版本、文件哈希锁
targets/            CPU 平台目标：CPU/AOT、接口能力名称和部署协议
lib/aster/          APP C runtime、日志、绘图和文字助手
apps/               示例应用，含 Star Catch 小游戏、国际象棋和不依赖显示的 hello
toolchain/          Linux x86_64 工具链及 Windows 打包入口
tools/              创建项目、构建、验证和 APP 部署
tests/              契约、目标选择、能力、内存、设备身份和构建测试
```

## AI 开发手册与 Skill

[APP 开发手册](docs/app-development.md) 是 ABI 3.13 的自包含开发参考，包含完整工程、准确接口签名、资源与内存规则以及构建部署命令。AI 可以只读取这一份手册来创建工程、编写 APP 和执行构建，无需阅读 SDK/固件源码或工具实现。

项目提供 [aster-app-development Skill](.agents/skills/aster-app-development/SKILL.md)，入口引用同一份手册。从 SDK 根目录启动支持项目 Skill 的 AI 工具后，可请求“使用 aster-app-development 创建 APP”；其他工具直接读取手册即可。工程可创建在 SDK 外。复制 Skill 到其他位置时需一并携带手册并修正链接。

## 安装与创建独立 APP

发布者推荐使用 [Ubuntu 双平台发布指南](docs/release.md)：`bash release.sh --hosts linux-x64,windows-x64` 从同一工作区生成 Windows 便携 ZIP 与 Linux 包，每个主机平台一个包，当前携带 `xtensa` CPU 目标。APP 构建与部署用 `--target xtensa`；具体开发板和固件烧录由固件仓库管理。接口更新通过固件契约同步，AOT 编译器按 CPU 平台绑定并复用。

Windows 和 Linux 共用 `aster` 命令，安装、环境激活、模板、构建和部署详见
[跨平台 SDK 命令指南](docs/sdk-cli.md)。Windows 原生发布包含三个 EXE、开发手册与 Skill，无需 WSL。构建发布包见 [发布说明](docs/release.md)。

Linux 便携包首次安装（Python 3.12；源码仓库改用 `./bin/aster`）：

```bash
./aster install --add-path
source ./env.sh
```

Windows 在三个 EXE 所在目录执行：

```powershell
.\aster.exe install --add-path
. .\env.ps1
```

激活后可在 SDK 之外开发，不需要项目构建脚本：

```text
aster doctor
aster templates
aster init my-game --template swipe-game --id com.example.mygame --target xtensa
cd my-game
aster build
aster run --port /dev/ttyUSB0
```

`scene-list` 提供 Scene 滚动设置页（需要 G7 Host 可选能力，见手册第 13 节）。`hello` 是最小示例，`swipe-game` 提供滑动触摸控制和四种音效。CPU 平台写入项目 `app.toml`；`build` 的输出在项目的 `build/<target>/` 和 `dist/<target>/`。可通过 `--target` 临时选择 CPU 平台，`--no-aot` 仅用于模拟器/主机测试生成 WASM。真实设备只运行 AOT，缺少 AOT 或加载失败时会明确报错。AOT 后端由目标决定，缺少所需编译器时不会静默退回解释执行。

原有 `python tools/aster.py new/build/verify/deploy` 仍可使用。批量编译 SDK 示例使用 `python tools/aster.py build --all --target xtensa --aot`。底层 `tools/build.py` 要求显式 CPU 平台或 `ASTER_TARGET`；统一 `aster` 入口优先读取项目 CPU 平台。

应用在 `app.toml` 声明 `requires` 和 `permissions`；使用 `aster_system_has_feature()` 判断可选能力，用 `aster_display_get_info()` 获取实际 APP 坐标空间和屏幕信息。不要把目标型号、引脚或屏幕尺寸写进业务分支。`apitest` 等已有诊断示例包含特定资源边界测试，不代表所有分辨率都已实机验证。

Wi-Fi APP 声明 `requires = ["wifi"]` 和 `permissions = ["wifi"]`，然后通过异步扫描、连接和状态接口操作网络。用法与能力边界见 [Wi-Fi API 指南](docs/wifi.md)。

六轴 IMU APP 使用 ABI 3.7 的 `aster_imu_get_info()` / `aster_imu_read()` 获取同步数据，配合 SDK `aster_imu_filter.h` 做姿态融合。完整规则见开发手册；[IMU 立方体示例](apps/imucube/README.md) 提供随动显示、归零与静置校准。

通过 [Wall clock API](docs/time.md) 读取 UTC 毫秒时间戳和设备本地日历时间；SNTP 由固件在后台维护，APP 可检查时间是否已经同步。`aster_time_now_ms()` 继续提供单调运行时间。

构建会检查 ABI、能力名称、内存页对齐、导入符号和生命周期导出。包内 `product` 与 `target_id = "xtensa"` 标识产品和 CPU 平台；同一个 APP 包可用于立创与微雪 ESP32-S3。各板内存上限由固件配置，实际屏幕和可选能力在运行时查询。

## 校验和部署

```bash
python tools/aster.py verify --target xtensa dist/xtensa/aster.gfxshow.ast
python tools/aster.py deploy dist/xtensa/aster.gfxshow.ast \
  --target xtensa --port /dev/ttyUSB0 --run
```

新固件在 shell 串口同时提供日志与文件服务，SDK 自动共用该端口完成传输、安装和启动；例如 Windows `--port COM24`，Linux `--port /dev/ttyUSB0`。旧固件仍可显式指定独立 `--console`。上传前关闭独立监视器，结束后在同一端口恢复。详见 [单串口指南](docs/serial-console-transfer.md)。

设备须事先安装匹配的 Aster 固件和系统资源。部署先验证包，再通过设备握手确认产品、目标、ABI 与存储状态，接着传输、安装并按需启动 APP。SDK 不构建或修改固件；Windows 独立设备工具可按用户指定的串口和偏移烧录已构建的固件镜像。不自动连接 USB 设备到 WSL。

容器与协议标识在改名中一并改掉了：包魔数 `ASTP`、图标魔数 `ASTI`、资源块 `ASTR`、文件传输 `ASTERFS`。旧 `MJA1`/`MJXI`/`MJR1` 记号一律不再被接受，因此改名前的包与改名前的固件互相拒收——新 SDK 的 API 为 `aster_*`，不直接运行旧 `mjx_*` 二进制，请重编应用。新版固件同样会拒绝旧产品或不同 CPU 平台的包。

开发用主机 ABI 测试程序可以直接运行 `build/xtensa/aster.hello/app.wasm`，无需新增硬件目标；该程序由固件仓库的 `tools/host.py` 构建，属于独立开发测试工具，不是 SDK 的源码依赖或正式部署设备。

同一 `xtensa` APP 包可在兼容设备的横竖屏模式下运行。部署时会核对设备当前能力，因此强依赖显示的 APP 在无屏模式下会被拒绝；不依赖显示的 APP 不受影响。已经安装的 APP 在固件启动时同样检查能力。

## 升级接口契约

```bash
python tools/import_contract.py /path/to/aster-contract-3.12.0.zip
python -m unittest discover -s tests -v
```

导入前校验完整文件清单和 SHA-256，拒绝路径穿越和损坏内容。导入工具把固件板卡描述转换为已登记编译器支持的 CPU 平台目标，保留平台与硬件的边界，并重新生成哈希锁。升级后一起审阅、提交 `include/`、`contract/`、`targets/`。正常构建只读取仓库内固定副本，既不会搜索兄弟目录，也不会自动拉取或同步固件源码。

本仓库保留 MIT 许可；工具链、图标等第三方组件保留各自许可，见 [NOTICE.md](NOTICE.md) 和 [toolchain/README.md](toolchain/README.md)。

## APP 屏幕方向

当前立创与微雪 ESP32-S3 的流光桌面固定为竖屏。应用在 `app.toml` 的 `[app]` 下配置：

```toml
orientation = "landscape" # 或 "portrait"；不设置时从竖屏启动
```

方向在应用初始化前生效；应用应查询实际显示尺寸。退出、停止或启动失败后，固件恢复竖屏 launcher。SDK 和驱动的横屏支持保持可用。

## 桌面文字与 APP 字体

流光桌面从安装包读取 APP 名称、简介和图标；桌面与 APP 文字统一使用固件提供的完整 SD 字库和按字号申请的 PSRAM 字形缓存。共有 12/14/16/18/20/24/28/32/40/48px 原生字号，使用 `aster_text_for_display` 按屏幕选择字号，再按实际行高布局。APP 安装、改名或升级不需要向固件添加内置字形。APP 初始化前准备常用字号，退出后释放缓存；返回桌面重新申请和预热。ABI 3.7.0 与包格式保持兼容，新的字号度量契约需要匹配设备资源发布。详见 [开发手册](docs/app-development.md#字体和中文)。

## 完整开发套件

从 SDK Release 下载对应平台的完整包，即得到 APP SDK、两款板的固件镜像、SD 字体资源和便携模拟器。固定版本和校验值在包内 development-kit.json；使用与模拟边界见 [完整开发套件](docs/development-kit.md)。开发 APP 无需 Zephyr、ESP-IDF 或 Espressif 驱动库。


## 原生字体与离线图片准备（G8）

保持原字号与16级字形覆盖率，不通过放大小字或二值化提高所谓清晰度。G8 固件修正RGB565混合的亮边偏差，以及文字指定背景后仍混入旧画面的边缘；无新增import，已有APP无需重编。开发规则见手册第14节。

`tools/prepare_image.py` 可从原图一次生成实际显示尺寸的RGB PNG/BMP：像素画选择 `--kind pixel`（最近邻），照片选择 `--kind photo`（Lanczos）。这是可选Python+Pillow素材工具，普通APP编译不依赖它；便携SDK的三个EXE没有新增图片处理子命令。透明素材需明确底色，launcher独立alpha图标继续使用ASTI RGB565A8。

当前完整开发套件：[`sdk-v3.7.0-20261015.1`](https://github.com/hello-myj/aster-sdk/releases/tag/sdk-v3.7.0-20261015.1)。本版同步图形/Scene、字形清晰度及launcher交互改进，固件内核堆为96 KiB；公开ABI保持3.7.0，尚未增加HTTP/TCP/MQTT接口。配套版本与验证边界见[完整开发套件](docs/development-kit.md)。

工作区开发版ABI3.8新增有界TCP/UDP，尚未发布为Release；此前3.7发布包不能运行这些新导入。见[网络开发说明](docs/network.md)和[Echo示例](apps/network-echo/README.md)。

ABI3.9阶段已完成HTTP/HTTPS的N2双板验收，未发布Release；参见[HTTP开发说明](docs/http.md)及开发手册第16节。

ABI 3.10 工作区增加 [MP3 分段流输入](docs/audio-stream.md) 和 [在线音乐示例](apps/online-music/README.md)，未发布 Release。

ABI 3.11 工作区增加有界 MQTT/MQTTS 客户端及 [MQTT Echo](apps/mqtt-echo/README.md)，尚未发布 Release。

ABI 3.12 工作区增加有界 WebSocket/WSS 客户端及 [WS Echo](apps/ws-echo/README.md)，尚未发布 Release。

ABI 3.12 工作区另附 [HTTP Resume](apps/http-resume/README.md) N5.4 参考示例（Range/If-Range 续传纪律，固件与 ABI 不变），未发布 Release。

ABI 3.13 工作区增加 `ASTER_SYS_NET_STATE` 网络状态通知（仅常量，无新函数），尚未发布 Release。
