# MQTT Echo

连接 MQTT 3.1.1，订阅并发布一条消息。需要 `mqtt` 和 `net`。MQTTS 再声明 `tls`。当前 ABI 为 4.0.0。

```bash
aster build apps/mqtt-echo --target xtensa --aot
aster deploy apps/mqtt-echo/dist/xtensa/aster.mqtt.echo.ast --target xtensa --port <串口> --run
```

先在系统设置里联网，再把 `app.toml` 里的主机改成 broker 地址。
