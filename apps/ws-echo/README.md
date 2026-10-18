# WS Echo（ABI 3.12 工作区）

有界、非阻塞的 RFC 6455 WebSocket 回环示例。连接后交替发送文本、二进制与合法空消息，逐字节校验回送内容与 opcode/长度边界；移动方块用于观察绘制与网络并发。点按暂停/继续发送；空闲保活 ping/pong、证书校验与关闭收尾都由固件完成，APP 不接触 socket、TLS 或 shell。

1. 准备设备可直连的 WS/WSS 回显服务（如固件仓库 `tools/validation/network_ws_peer.cjs` 的 `/echo`）；在 `app.toml` 的 `build.defines` 中设置 `WS_HOST`、`WS_PORT`。默认 127.0.0.1:17880 用于模拟器本机，实机改成设备能访问的局域网地址。
2. 使用当前 ABI 3.12 SDK 执行 `aster build apps/ws-echo --target xtensa --aot`。连接硬件 Wi-Fi 后，通过 `aster deploy apps/ws-echo/dist/xtensa/aster.ws.echo.ast --port PORT` 部署。
3. 模拟器运行同一包时加 `--network`，见模拟器帮助；真实网络不能放进确定性 replay。
4. WSS 设置 `WS_TLS=1`、TLS 端口，并在 requires 中添加 `tls`。证书由设备管理员管理，APP 不能跳过校验。全设备只有一个 WebSocket 客户端，与 CLI/HTTP/MQTT/TCP 共用两个传输名额和一个 TLS 名额，占用时 connect 返回明确错误，不是排队等待。

完整类型、接口、边界及错误处理见 [开发手册第19节](../../docs/app-development.md#19-websocketwssabi-312-工作区开发版)。该版本尚未发布 Release，SDK 要与 ABI 3.12 固件/模拟器配套。
