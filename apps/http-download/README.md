# HTTP Download

把固定长度的响应写到 `/data/download.bin` 并核对内容。需要 `http`、`filesystem` 和 `net`、`fs`。

清单中的应用 ID 为 `aster.http.download`，最低 ABI 为 `4.0`，启动所需能力为 `http`、`filesystem`。

```bash
aster build apps/http-download --target xtensa --aot
aster deploy apps/http-download/dist/xtensa/aster.http.download.ast --target xtensa --port <串口> --run
```

真机改 `app.toml` 的 `HTTP_URL`。
