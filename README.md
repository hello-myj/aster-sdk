# Aster SDK

应用开发仓库。代码包含 `<aster/aster.h>`，通过 `aster_*` 调用设备功能。当前 ABI 为 4.1.0，CPU 目标是 `xtensa`，同一包可用于立创和微雪 ESP32-S3。3.x 包需要用本 SDK 重新编译后再安装。

网络代码继续调用 `aster_network_*`、`aster_http_*`、`aster_mqtt_*`、`aster_ws_*`。这些函数在 `lib/aster/aster_netlib.c` 里转到六个导入：`aster_net_info`、`aster_net_open`、`aster_net_io`、`aster_net_status`、`aster_net_cancel`、`aster_net_close`。应用 manifest 的 `abi_min` 设为 `4.0`。

接口说明在 [APP 开发手册](docs/app-development.md)。

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
aster verify dist/xtensa/<应用>.ast --target xtensa
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
python tools/import_contract.py /path/to/aster-contract-4.1.0.zip
python -m unittest discover -s tests -v
```

导入后一起提交 `include/`、`contract/` 和 `targets/`。日常构建只读仓库里的副本。

发布步骤见 [docs/release.md](docs/release.md)。命令说明见 [docs/sdk-cli.md](docs/sdk-cli.md)。许可见 [NOTICE.md](NOTICE.md)。

当前正式版本为 `sdk-v4.1.0`；下载本机平台的完整开发包即可获得 APP SDK、匹配的两款固件和便携模拟器。功能边界与已完成验证范围见 [连续音效说明](docs/continuous-audio.md)。
