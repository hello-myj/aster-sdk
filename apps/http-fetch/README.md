# HTTP Fetch

用 GET 读取一小段 JSON，并在画面上显示结果。需要 `http` 和 `net`。

清单中的应用 ID 为 `aster.http.fetch`，最低 ABI 为 `4.0`，启动所需能力为 `http`。

```bash
aster build apps/http-fetch --target xtensa --aot
aster deploy apps/http-fetch/dist/xtensa/aster.http.fetch.ast --target xtensa --port <串口> --run
```

默认 URL 指向本机测试服务。真机改 `app.toml` 的 `HTTP_URL`。
