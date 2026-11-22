---
name: aster-app-development
description: 使用 Aster APP SDK ABI 4.5 开发 C/WASM/AOT 应用。当用户要求创建 Aster APP 工程、编写游戏或图形应用、添加触摸交互和设备功能、构建打包部署，或要求只读开发手册进行开发时使用。基于自包含手册，不需要读取 SDK、固件或工具源码。4.0 不兼容 3.x，旧 APP 必须重编后重装。
---

# Aster APP 开发

默认按架构开发。新建工程用 `aster init 工程 --template hello --id com.example.hello --target riscv` 或 `--target xtensa`，已有工程执行 `aster build` 沿用 `[sdk].target`。同一源码按需要为两个架构分别构建，默认产物在 `dist/<架构>/<应用ID>.ast`。布局读取 `aster_display_get_info`，可选外设用 `aster_system_has_feature` 查询，并在 `[app]` 区分 `requires` 和 `optional`。

同架构、同 AOT 配置的新板直接使用架构包。开发 APP 不要求板号，也无需读取固件仓库或增加 SDK 板型文件。固件每次启动时检查 `requires`；可选能力由 APP 查询并降级。`--hardware` 仅作为已有板型的架构简称使用，如 `--hardware fakepod-s31` 选择 `riscv`，产物独立写入 `dist/riscv/fakepod-s31/`。通用 APP 省略 `supported_boards`。专用 APP 可在 `[app]` 中声明 `supported_boards = ["fakepod-s31"]`，使用设备报告的 `board` 原值，可列出多个 ID。新版固件在启动时匹配；可用 `aster verify 文件.ast` 离线读取声明。商城按包声明筛选，发布配置可以缩小分发范围。实测机型按发布版本另行记录。

烧录固件时显式选择板型和串口。先构建，再用 `aster sim --profile osptek-p4 --app 工程目录`（或 `--profile lckfb`）调试，外部资源可用 `--resources` 指定。模拟器执行 WASM，实机执行 AOT。串口安装前返回设备桌面，安装包应包含所需资源。详见[开发包使用说明](../../../docs/使用说明.md)。

首先完整读取 [SDK 开发手册](../../../docs/app-development.md)。该路径相对本 SKILL.md；它是唯一开发参考，包含完整示例、公开 ABI、辅助库、工程配置和命令。

按手册第 1 节执行流程：确定 SDK 路径和项目目录，创建 app.toml 与 C 源码，执行构建与包校验，用户需要时部署。命令可执行，工具源码不需要阅读。不要扫描固件、SDK 头文件或模板实现来补充知识，也不要修改 SDK 固定契约。

根据第 4 节示例建立生命周期和绘图流程；其他能力使用第 5–18 节明确列出的接口。手册未提供的接口不能猜测；说明缺少的能力并询问是否需要 SDK 扩展。项目业务代码可以正常读取和修改。

部署按手册“真机部署”执行：新固件优先使用一个 `--port`，由 SDK 探测共享串口；旧固件才显式指定独立 `--console`。上传或截图前释放独立串口监视器。本次传输升级不改变 APP ABI，也不要求重编已有 APP。

完成时交付工程位置、构建命令、包路径和实际验证范围。缺少设备时明确真机待验证，不把构建成功写成设备运行成功。

此 Skill 随 SDK 项目使用。若复制 Skill 到其他技能目录，必须同时复制手册并更新上面的相对链接；不能只复制此入口文件。

包含多个保留对象、设置列表或按钮时，按手册第 13 节选用 Scene 辅助层，可使用 `scene-list` 模板。先确认 Host 支持帧裁剪及字体 epoch；旧版同 ABI Host 也可能不支持，初始化返回 ENOTSUP 时需升级配套固件/模拟器。对象池、按钮表和命令区由 APP 持有；不直接修改节点字段，不混用其他绘制器。将触摸交给 widgets，pause/resume 取消捕获，resume 使 Scene 失效；没有变化时不要主动 invalidate。Scene 不保证面板支持局部传输。

字体与图片质量按手册第14节处理：保持原生小字号和整数基线；透明文字更新先恢复旧背景。像素画与照片按实际显示尺寸选择采样；可选素材脚本依赖Pillow，不是APP编译或固件环境依赖。不要把GFX的黑色透明键误当独立alpha。

网络应用的 `abi_min` 为 4.0。TCP/UDP 按第 15 节，需要 `network` 和 `net`。HTTP 按第 16 节，MQTT 按第 18 节，WebSocket 按第 19 节。调用原来的函数名。全设备两个连接槽，TLS 一个名额。在事件里轮询，`EAGAIN` 下次再试。终态句柄要 close。模拟器真实通信加 `--network`，不与回放同时使用。

MP3 边下载边播放按第 17 节，需要 `audio-out` 和 `audio-stream`。`write` 可能只接受一部分。HTTP 成功结束才 `finish`。模拟器不提供 `audio-stream`。

持续引擎音等使用 ABI 4.1 的连续音效，先读 [连续音效说明](../../../docs/continuous-audio.md)。play_ex 启动一次；参数变化使用 set_params，单独停止使用 effect_stop。新导入必须 abi_min=4.1；不要用低频 FRAME 反复 play 模拟循环。首版切歌、流式音乐交接和 SD 压力连续性尚未验收，不能承诺无缝。

ABI 4.4 起提供可选 `media-player` 服务，先读手册文末 ABI 4.4 节。系统会话可跨 APP 退出保留；不要退出时自动停止。蓝牙接收与本地播放按能力查询和异步状态切换，无后端返回 ENOTSUP；源码模拟器可解析 ABI 4.5 并链接可选播放器导入，未实现播放后端时返回 ENOTSUP。配套 Windows 模拟器 1.1.0 使用 ABI 4.5；需要 `media-player` 的应用在 FAKE POD 实机验证。

音乐 APP 可检查 `ASTER_PLAYER_CAP_SYSTEM_OUTPUT`。支持时给 `PLAY_FILE` 传 `ASTER_PLAYER_OUTPUT_SYSTEM`，由固件在音箱连接后接管当前播放。旧固件的选路方式见手册 ABI 4.5 蓝牙音频输出一节。

蓝牙耳机/音箱输出使用手册 ABI 4.5 节，先检查 BT_SOURCE capability，查询有界设备列表，再按蓝牙地址连接。发送模式和接收模式互斥，文件路径仍受沙箱约束；SBC 是有损，不能标作无损输出。

安装前用 `aster verify PACKAGE` 读取包内架构、`supported_boards` 和 `requires`。串口 `aster deploy` 接受一个 AST；独立资源目录走商城安装或桌面 `aster sim --resources`。示例的最低 ABI 以其 `app.toml` 为准。
