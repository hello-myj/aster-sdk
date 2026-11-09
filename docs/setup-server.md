# 首次开机资源服务器

SDK 提供 `aster server`，使用 Python 标准库；Windows、WSL 已验证，macOS 尚未实测。
当前用于局域网初始化。与 APPS 仓库的商城服务器配合使用：初始化服务默认 9081，
商城目录与游戏包仍由商城服务器的 9080 提供。两者职责和数据目录独立。

## 发布资源

先编译对应硬件的商城 AOT 包。服务器不接受上传或在线发布，发布只在电脑命令行执行。
在 SDK 根目录运行（路径按本机调整）：

```powershell
python tools/aster.py server --data build/setup-data publish `
  --fonts ../aster-firmware/resources/fonts `
  --store ../aster-apps/AppStore/dist/riscv/aster.store.ast `
  --board osptek-p4-st7102 --target riscv --screen 480x480 `
  --version 1.0.0 --market-url http://192.168.110.154:9080

python tools/aster.py server --data build/setup-data publish `
  --fonts ../aster-firmware/resources/fonts `
  --store ../aster-apps/AppStore/dist/xtensa/aster.store.ast `
  --board aster.esp32s3.lckfb --target xtensa --screen 240x320 `
  --version 1.0.0 --market-url http://192.168.110.154:9080

python tools/aster.py server --data build/setup-data start --host 0.0.0.0 --port 9081
```

`--data` 是 `server` 的全局参数，放在 `publish/start` 前面。不指定时使用用户目录下
的 `.aster/setup-server`。已打包的 SDK 使用 `aster server ...`，省略 `python tools/aster.py`。
`--board` 使用设备 `aster_system_info_t.board` 的运行时标识；lckfb 当前为
`aster.esp32s3.lckfb`，与构建预设名 `lckfb-s3` 不同。
完整字体目前约 92 MiB（96 MB），包含 12/14/16/18/20/24/28/32/40/48px 十个字号，不能用屏幕 UI
中的少量固定字代替。发布会检查每个 cpfont 的结构和所有字形范围，校验商城 AST/AOT，
再复制成只读发布快照。每个硬件有独立的当前发布指针；发布被打断时保留上一版本。

手机连接设备屏幕上的临时热点，打开 `http://192.168.4.1`，输入家庭 Wi-Fi 和初始化
服务地址，例如 `http://192.168.110.154:9081`。电脑和设备的家庭 Wi-Fi 必须能互通；
使用电脑的局域网 IP，不能填 `127.0.0.1`。允许服务器端口通过本地防火墙。
设备连接路由器后自行下载，不通过手机转发文件。`--market-url` 会写入商城配置，
因此该地址也必须是设备能访问的地址。

## 设备流程与协议

SD 卡是系统依赖。已有完整字体和本机兼容商城时，设备离线启动，不访问服务器。
缺少依赖时使用固件中统一的系统 UI 字库显示欢迎、热点及进度；APP 完整字库和商城
均下载到 SD。商城没有嵌入固件。设备先校验并放置字体，最后通过现有事务安装器
安装 `aster.store`。原有应用、存档和数据不被清理。

- `GET /healthz`：服务存活检查。
- `GET /v1/setup/resolve?board=...&target=...&abi=...&width=...&height=...&features=...`：
  按设备标识、AOT CPU、ABI 主版本/最低版本、屏幕和能力位匹配，返回不可变 release ID。
- `GET /v1/setup/releases/{release}/files/{index}`：单个文件的大小、SHA-256、相对路径与下载 URL。
- `GET/HEAD /v1/setup/releases/{release}/payload/{index}`：文件，支持单区间 Range、If-Range、ETag。

设备只允许十个固定字体路径和一个商城包，不把服务器提供的任意路径映射到 SD。
下载写入 `/aster/system/setup/<sha256>.part`，支持恢复，完成后校验整个文件再改名。
已经存在且摘要一致的文件不重复下载。网络错误保留部分文件，错误页可重新配网。
断电后，若已有成功连接的 Wi-Fi 配置与资源服务器地址，设备会尝试继续。
初始化会保留一个未完成标记，确保资源文件写完但服务器配置尚未提交时，重启仍能继续收尾。
写入前检查可用空间，安装商城另外预留空间。存储卡在 I/O 中拔出会报错；重新插卡
仍不能访问时需要重启设备，不做自动格式化。

设备页面和 Web 接口仅绑定临时热点地址，配网提交需要本次随机令牌，不在日志中
输出 Wi-Fi 密码。初始化完成后关闭热点和配网页面，释放临时下载缓冲与线程栈。
若需要重新进入引导，先停止 APP：P4 使用 `aster ui setup`，Zephyr 使用
`aster sys setup`。该维护入口保留原有资源，不等同于恢复出厂设置。

## 后续公网部署

保持上述接口即可迁移，不必改变 APP 包格式。将只读资源服务放在经过证书验证的
HTTPS 域名后，通过 Web 配网修改设备的资源服务地址；`--market-url` 同样使用 HTTPS。
局域网 HTTP 的 SHA-256 负责发现损坏，不提供服务器身份认证。当前未部署公网服务，
也没有开放网络发布接口。资源版本与商城 APP 版本分别管理，商城后续按已有更新机制更新。

## 验证

```sh
python -m unittest discover -s tests -p test_setup_server.py
```

覆盖兼容性拒绝、结构损坏、断点请求、路径边界、只读接口和中断发布。
实机验证状态见固件仓库 `docs/first-boot-setup.md`。
