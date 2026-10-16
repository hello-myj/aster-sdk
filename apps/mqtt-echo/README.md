# MQTT Echo（ABI 3.11 工作区）

有界、非阻塞的 MQTT 3.1.1 示例。连接后订阅 `aster/example/echo`，每秒发布一条 QoS1 消息，逐字节校验 broker 回送内容；移动方块用于观察绘制与网络并发。点按暂停/继续发布，网络心跳仍由固件运行。没有循环阻塞等待，也不需要 APP 自己维护 socket。

1. 准备设备能直连的 MQTT broker；在 `app.toml` 的 `build.defines` 中设置 `MQTT_HOST`、`MQTT_PORT`。默认 127.0.0.1:1883 用于模拟器本机。多设备同时测试时修改源码中的 client ID，避免 broker 互踢。
2. 使用当前 ABI 3.11 SDK 执行 `aster build apps/mqtt-echo --target xtensa --aot`。连接硬件 Wi-Fi 后，通过 `aster deploy apps/mqtt-echo/dist/xtensa/aster.mqtt.echo.ast --port PORT` 部署。
3. 模拟器运行同一包时加 `--network`，见模拟器帮助；真实网络不能放进确定性 replay。
4. MQTTS 设置 `MQTT_TLS=1`、broker TLS 端口，并在 requires 中添加 `tls`。证书由设备管理员管理，APP 不能跳过校验。不要把生产密码写入示例源码。

完整类型、接口、边界及错误处理见 [开发手册第18节](../../docs/app-development.md#18-mqttmqttsabi-311-工作区开发版)。该版本尚未发布 Release，SDK 要与 ABI 3.11 固件/模拟器配套。
