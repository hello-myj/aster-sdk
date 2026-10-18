---
name: aster-app-development
description: 使用 Aster APP SDK ABI 3.12 开发 C/WASM/AOT 应用。当用户要求创建 Aster APP 工程、编写游戏或图形应用、添加触摸交互和设备功能、构建打包部署，或要求只读开发手册进行开发时使用。基于自包含手册，不需要读取 SDK、固件或工具源码。
---

# Aster APP 开发

首先完整读取 [SDK 开发手册](../../../docs/app-development.md)。该路径相对本 SKILL.md；它是唯一开发参考，包含完整示例、公开 ABI、辅助库、工程配置和命令。

按手册第 1 节执行流程：确定 SDK 路径和项目目录，创建 app.toml 与 C 源码，执行构建与包校验，用户需要时部署。命令可执行，工具源码不需要阅读。不要扫描固件、SDK 头文件或模板实现来补充知识，也不要修改 SDK 固定契约。

根据第 4 节示例建立生命周期和绘图流程；其他能力使用第 5–18 节明确列出的接口。手册未提供的接口不能猜测；说明缺少的能力并询问是否需要 SDK 扩展。项目业务代码可以正常读取和修改。

部署按手册“真机部署”执行：新固件优先使用一个 `--port`，由 SDK 探测共享串口；旧固件才显式指定独立 `--console`。上传或截图前释放独立串口监视器。本次传输升级不改变 APP ABI，也不要求重编已有 APP。

完成时交付工程位置、构建命令、包路径和实际验证范围。缺少设备时明确真机待验证，不把构建成功写成设备运行成功。

此 Skill 随 SDK 项目使用。若复制 Skill 到其他技能目录，必须同时复制手册并更新上面的相对链接；不能只复制此入口文件。

包含多个保留对象、设置列表或按钮时，按手册第 13 节选用 Scene 辅助层，可使用 `scene-list` 模板。先确认 Host 支持帧裁剪及字体 epoch；旧版同 ABI Host 也可能不支持，初始化返回 ENOTSUP 时需升级配套固件/模拟器。对象池、按钮表和命令区由 APP 持有；不直接修改节点字段，不混用其他绘制器。将触摸交给 widgets，pause/resume 取消捕获，resume 使 Scene 失效；没有变化时不要主动 invalidate。Scene 不保证面板支持局部传输。

字体与图片质量按手册第14节处理：保持原生小字号和整数基线；透明文字更新先恢复旧背景。像素画与照片按实际显示尺寸选择采样；可选素材脚本依赖Pillow，不是APP编译或固件环境依赖。不要把GFX的黑色透明键误当独立alpha。

网络应用按手册第15节：公共TCP/UDP导入需要ABI最低3.8、network能力及net权限。Wi-Fi配置另需wifi权限；连接/发送入队不代表成功。使用事件驱动轮询与有界缓冲，处理EAGAIN、部分TCP接受、UDP短缓冲和EOF，退出自动回收。模拟器真实通信显式加--network，不能与确定性replay混用；HTTP/HTTPS按第16节使用ABI最低3.9、http/tls能力；MQTT/MQTTS按第18节使用ABI最低3.11、mqtt能力及net权限；使用操作序号查询完成结果，及时排空两条消息队列；全系统共享两个传输连接和一个TLS名额。WebSocket/WSS按第19节使用ABI最低3.12、websocket能力及net权限（wss另需tls）；单设备一个客户端，保留文本/二进制消息边界，length=0为合法空消息，recv EAGAIN表示暂无消息而非空；发送OK只是入队，EAGAIN稍后重试不盲目重发；CLOSED取完缓冲后为EPIPE，终态后必须close再connect；空闲保活与分片超时由固件负责，不自动重连。

MP3 边下载边播放按手册第17节：ABI最低3.10、audio-out/audio-stream能力；保留write部分接受尾部，HTTP成功EOF才finish，检查DONE后close，退出自动回收。模拟器尚不支持audio-stream。
