# MQTT Echo

连接 MQTT 3.1.1，订阅并发布一条消息。需要 `mqtt` 和 `net`。MQTTS 再声明 `tls`。

清单中的应用 ID 为 `aster.mqtt.echo`，最低 ABI 为 `4.0`，启动所需能力为 `mqtt`、`network`。

```bash
aster build apps/mqtt-echo --target xtensa --aot
aster deploy apps/mqtt-echo/dist/xtensa/aster.mqtt.echo.ast --target xtensa --port <串口> --run
```

先在系统设置里联网，再把 `app.toml` 里的主机改成 broker 地址。
