# TCP/UDP Echo 示例（ABI 3.8）

仅需 APP SDK；不使用固件源码、Zephyr 或 Espressif 环境。设备需刷入配套 ABI 3.8 固件并先通过系统设置联网。

1. 电脑启动 `node tools/network_echo_server.cjs`，记录电脑局域网 IPv4。
2. 真机：修改 `app.toml` 的 `NETWORK_HOST` 为电脑 IPv4；两者连接同一局域网。模拟器保留127.0.0.1，并启用 `--network`。
3. SDK根目录运行 `python tools/aster.py build apps/network-echo --target xtensa`，再按开发手册部署输出的 `.ast`；模拟器直接运行该包的WASM。
4. 成功日志依次为TCP512字节、UDP1200字节（先短缓冲再完整重读）、空数据报校验，最终 `NETAPP PASS rounds=1` 后退出。

应用只声明 `net` 权限及 `network` 能力，不获得配置Wi-Fi的权限。网络调用在帧事件中按状态推进，不用忙循环；EAGAIN留到下一帧再重试。发送返回的是入队量，实际返回内容需逐字节验证。详见 `docs/network.md`。

测试服务器只做原样echo；最多4条TCP连接，每条回送512字节后关闭，空闲10秒断开；UDP只回送不超过1200字节的数据报，十分钟后自动退出。可按Ctrl+C提前停止。它不需要第三方Node模块，也不是生产服务器。
