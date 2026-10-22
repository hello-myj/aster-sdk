# TCP/UDP Echo

在帧事件里收发 TCP 和 UDP，并核对回包。需要 `network` 能力和 `net` 权限。当前 ABI 为 4.1.0。

```bash
node tools/network_echo_server.cjs
python tools/aster.py build apps/network-echo --target xtensa --aot
aster deploy apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa --port <串口> --run
```

真机把 `app.toml` 的 `NETWORK_HOST` 改成电脑的 IPv4。模拟器保留 `127.0.0.1`，启动时加 `--network`。成功日志以 `NETAPP PASS` 结束。
