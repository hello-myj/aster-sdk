# HTTP/HTTPS

当前 ABI 为 4.0.0。应用调用 `aster_http_request`、`aster_http_status`、`aster_http_read`、`aster_http_header`、`aster_http_cancel`、`aster_http_close`。

```toml
abi_min = "4.0"
permissions = ["net"]
requires = ["http"]
```

HTTPS 再加 `"tls"`。写文件再加权限 `"fs"` 和能力 `"filesystem"`。

`request` 返回正句柄只表示任务已接收。在帧事件里查状态并读取。HTTP 404/500 仍是传输完成，用 status 里的状态码判断。`read` 返回 0 表示读完。

- 与 TCP/UDP、MQTT 共用两个连接槽；HTTPS 与 MQTTS、WSS 共用一个 TLS 名额。
- 请求体最多 2048 字节，URL 最多 1024 字节，头部最多 2048 字节 / 32 个字段，响应队列 2048 字节，单次读取 1..2048 字节。
- 超时 1..600000 ms。重定向最多 3 次，拒绝从 HTTPS 降到 HTTP。
- 默认信任根是 ISRG Root X1。应用不能更换根证书或跳过校验。

```bash
aster build apps/http-fetch --target xtensa
aster build apps/http-download --target xtensa
```

示例默认访问 `127.0.0.1`。测试服务在固件仓库：

```bash
node tools/validation/network_http_server.cjs
```

真机把 `app.toml` 的 `HTTP_URL` 改成自己的服务地址后再构建。模拟器：

```bash
node src/cli.js <应用>.ast --network
```

续传示例是 `apps/http-resume`。
