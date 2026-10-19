# WebSocket Echo

连接 `ws://` 或 `wss://`，发送并收回一条消息。需要 `websocket` 和 `net`。WSS 再声明 `tls`。当前 ABI 为 4.0.0。

```bash
aster build apps/ws-echo --target xtensa --aot
aster deploy apps/ws-echo/dist/xtensa/aster.ws.echo.ast --target xtensa --port <串口> --run
```

先联网，再把 `app.toml` 里的 URL 改成对端地址。
