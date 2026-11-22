# Aster SDK

应用开发仓库。代码包含 `<aster/aster.h>`，通过 `aster_*` 调用设备功能。当前 ABI 为 4.5.0，CPU 目标为 `xtensa`（ESP32-S3）和 `riscv`（ESP32-P4、ESP32-S31）。3.x 包需要用本 SDK 重新编译后再安装。

网络代码继续调用 `aster_network_*`、`aster_http_*`、`aster_mqtt_*`、`aster_ws_*`。这些函数在 `lib/aster/aster_netlib.c` 里转到六个导入：`aster_net_info`、`aster_net_open`、`aster_net_io`、`aster_net_status`、`aster_net_cancel`、`aster_net_close`。仅使用上述网络接口时，最低 ABI 可设为 4.0。使用后续新增导入时，按所用接口提高 `abi_min`；例如系统播放器的蓝牙设备枚举需要 4.5。

从[开发包使用说明](docs/使用说明.md)选择源码或便携包，再从 [APP 开发手册](docs/app-development.md)查询接口、权限和最低 ABI。每个示例 README 列出其清单声明的启动条件。

首次开机的字体和商城资源可由 SDK 本地服务提供，参见 [初始化资源服务器](docs/setup-server.md)。
免服务器发布见 [GitHub 静态分发](docs/github-distribution.md)，包含自动编译、发布及设备源配置。

## 选择 APP 架构

立创 ESP32-S3 实战派使用 `xtensa`，鱼鹰光电 P4C5 和 FAKE POD NANO PRO 使用 `riscv`。同一工程按需为每个架构构建 AOT 包。日常执行 `aster build` 即可使用项目的 `[sdk].target`。相同架构和 AOT 配置的新板可以直接使用架构包。设备每次启动 APP 时检查所需能力，已有脚本可继续用 `--hardware` 作为已知板型的架构简称。专用 APP 可在 `[app].supported_boards` 列出允许启动的板型 ID，用 `aster verify 文件.ast` 查看。命令及产物路径见[开发包使用说明](docs/使用说明.md)。

## 安装

Linux 源码仓库：

```bash
./bin/aster install --add-path
source ./env.sh
```

Windows 便携包：

```powershell
.\aster.exe install --add-path
. .\env.ps1
```

## 创建和运行

```text
aster doctor
aster init my-game --template swipe-game --id com.example.mygame --target xtensa
cd my-game
aster build
aster run --port <串口>
```

`hello` 是最小示例，`swipe-game` 带滑动和音效，`scene-list` 是滚动设置页。输出在项目的 `build/xtensa/` 和 `dist/xtensa/`。设备只运行 AOT。`--no-aot` 只用于模拟器和主机测试。

```bash
aster verify dist/xtensa/<应用>.ast
aster deploy dist/xtensa/<应用>.ast --target xtensa --port <串口> --run
```

握手返回 `serial=shared` 时，日志、安装和启动使用同一个串口。上传前关闭占用该口的监视器。

批量编译示例：

```bash
python tools/aster.py build --all --target xtensa --aot
```

## 能力

在 `app.toml` 声明 `requires` 和 `permissions`。运行时用 `aster_system_has_feature()` 和 `aster_display_get_info()` 查询能力和屏幕尺寸。

- Wi-Fi：`requires = ["wifi"]`，`permissions = ["wifi"]`，见 [docs/wifi.md](docs/wifi.md)。
- TCP/UDP：见 [docs/network.md](docs/network.md)。
- HTTP：见 [docs/http.md](docs/http.md)。
- 时间：见 [docs/time.md](docs/time.md)。

方向写在 `app.toml` 的 `[app]`：

```toml
orientation = "landscape"
```

不设置时从竖屏启动。退出后桌面回到竖屏。

## 更新契约

```bash
python tools/import_contract.py /path/to/aster-contract-4.5.0.zip
python -m unittest discover -s tests -v
```

导入后一起提交 `include/`、`contract/` 和 `targets/`。日常构建只读仓库里的副本。

发布步骤见 [docs/release.md](docs/release.md)。命令说明见 [docs/sdk-cli.md](docs/sdk-cli.md)。许可见 [NOTICE.md](NOTICE.md)。

源码契约为 ABI 4.5.0。已发布的便携开发包 `sdk-v4.3.0` 提供 ABI 4.3 工具，按需下载该版本固定的固件镜像和 Windows 桌面模拟器。开发 FAKE POD 的系统播放器请使用 4.5 源码工具链和相应固件。[SDK Release](https://github.com/hello-myj/aster-sdk/releases) · [组件与镜像](docs/development-kit.md)
