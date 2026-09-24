# Aster SDK

独立 APP 开发仓库。应用只包含 `<aster/aster.h>` 或使用 `lib/aster/` 的图形、文字助手，通过固件公开的 `aster_*` 接口调用系统功能。无需包含 Zephyr、芯片 SDK、LVGL、驱动或固件内部头文件。

```text
include/aster/       固定版本的公开接口副本
contract/           API 描述、运行时版本、文件哈希锁
targets/            发布目标：CPU/AOT、能力、屏幕信息和部署协议
lib/aster/          APP C runtime、日志、绘图和文字助手
apps/               10 个示例，含不依赖显示的 hello
toolchain/          Linux x86_64 应用编译工具链
tools/              创建项目、构建、验证和 APP 部署
tests/              契约、目标选择、能力、内存、设备身份和构建测试
```

## 编写和构建 APP

Python 3.11 或更新版本。当前工作区建议激活已有 Python 3.12 `.venv`。基础编译和离线校验只使用 Python 标准库；串口部署需 `pyserial`，签名验证需 `cryptography`，可用 `pip install -r requirements.txt` 安装。

以下命令在本仓库执行：

```bash
python tools/aster.py targets
python tools/aster.py new /tmp/my-aster-app --id com.example.hello
python tools/aster.py build /tmp/my-aster-app --target lckfb-s3 --no-aot
python tools/aster.py build apps/gfxshow --target lckfb-s3 --aot
python tools/aster.py build --all --target lckfb-s3 --aot
```

`--target` 选择真实硬件型号，当前只有立创 ESP32-S3 实战派 `lckfb-s3`，也可用 `ASTER_TARGET` 设置。横屏、竖屏、无屏是同一固件的运行模式，不是 SDK 目标。每个目标的中间产物和 APP 包分别放在 `build/<target>/`、`dist/<target>/`，切换目标不会覆盖另一目标。AOT 后端由目标决定，冲突的 `--aot-target` 会被拒绝。显式请求 AOT 却没有编译器时不会静默退回解释执行。

应用在 `app.toml` 声明 `requires` 和 `permissions`；使用 `aster_system_has_feature()` 判断可选能力，用 `aster_display_get_info()` 获取实际 APP 坐标空间和屏幕信息。不要把目标型号、引脚或屏幕尺寸写进业务分支。`apitest` 等已有诊断示例包含特定资源边界测试，不代表所有分辨率都已实机验证。

构建会检查 ABI、必需能力、内存限制、导入符号和生命周期导出。相同源码可以针对不同目标构建；包内的 `product` 和 `target_id` 用于防止装错设备。

## 校验和部署

```bash
python tools/aster.py verify --target lckfb-s3 dist/lckfb-s3/aster.gfxshow.ast
python tools/aster.py deploy dist/lckfb-s3/aster.gfxshow.ast \
  --target lckfb-s3 --port /dev/ttyACM0 --console /dev/ttyUSB0 --run
```

设备须事先安装匹配的 Aster 固件和系统资源。部署先验证包，再通过设备握手确认产品、目标、ABI 与存储状态，接着传输、安装并按需启动 APP。SDK 不构建、烧录或修改固件，不调整硬件配置，不自动连接 USB 设备到 WSL。

容器与协议标识在改名中一并改掉了：包魔数 `ASTP`、图标魔数 `ASTI`、资源块 `ASTR`、文件传输 `ASTERFS`。旧 `MJA1`/`MJXI`/`MJR1` 记号一律不再被接受，因此改名前的包与改名前的固件互相拒收——新 SDK 的 API 为 `aster_*`，不直接运行旧 `mjx_*` 二进制，请重编应用。新版固件同样会拒绝旧产品或不同目标的包。

开发用主机 ABI 测试程序可以直接运行 `build/lckfb-s3/aster.hello/app.wasm`，无需新增硬件目标；该程序由固件仓库的 `tools/host.py` 构建，属于独立开发测试工具，不是 SDK 的源码依赖或正式部署设备。

同一 `lckfb-s3` APP 包可在该设备的横竖屏模式下运行。部署时会核对设备当前能力，因此强依赖显示的 APP 在无屏模式下会被拒绝；不依赖显示的 APP 不受影响。已经安装的 APP 在固件启动时同样检查能力。

## 升级接口契约

```bash
python tools/import_contract.py /path/to/aster-contract-3.3.0.zip
python -m unittest discover -s tests -v
```

导入前校验完整文件清单和 SHA-256，拒绝路径穿越和损坏内容。升级后一起审阅、提交 `include/`、`contract/`、`targets/`。正常构建只读取仓库内固定副本，既不会搜索兄弟目录，也不会自动拉取或同步固件源码。

本仓库保留 MIT 许可；工具链、图标等第三方组件保留各自许可，见 [NOTICE.md](NOTICE.md) 和 [toolchain/README.md](toolchain/README.md)。
