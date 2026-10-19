# TCP/UDP

当前 ABI 为 4.0.0。应用调用 `aster_network_get_info`、`aster_network_open`、`aster_network_send`、`aster_network_recv`、`aster_network_status`、`aster_network_cancel`、`aster_network_close`。SDK 把它们转到 `aster_net_*`。

`app.toml`：

```toml
abi_min = "4.0"
requires = ["network"]
permissions = ["net"]
```

`wifi` 权限只用于改无线连接。先在系统设置里联网。

```bash
python tools/aster.py build apps/network-echo --target xtensa
python tools/aster.py deploy apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa --port <串口> --run
```

电脑运行 `node tools/network_echo_server.cjs`。真机把示例 `app.toml` 里的 `NETWORK_HOST` 改成电脑的 IPv4。模拟器保留 `127.0.0.1`，启动时加 `--network`。

在 FRAME 或 TIMER 里轮询。`ASTER_EAGAIN` 留到下一次事件。全设备两个连接槽，与 HTTP、MQTT、WebSocket 共用。HTTPS、MQTTS、WSS 另共用一个 TLS 名额。终态句柄要 `close` 才释放。

单次 IO 最多 2048 字节，UDP 最多 1200 字节。TCP 发送返回的是已入队字节数。UDP 容量不足时返回 `ASTER_EMSGSIZE`，原包保留。TCP 读到 0 表示对端关闭，之后不能再发送。

签名和结构见开发手册与 `include/aster/network.h`。
