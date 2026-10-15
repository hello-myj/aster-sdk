# HTTP Fetch

ABI 3.9 工作区示例：FRAME事件异步获取JSON、检查HTTP状态与Content-Type、验证受控数据并显示内容，动画持续运行。不是通用JSON解析器。

修改 `app.toml` 的 `HTTP_URL` 为实际服务；本例默认匹配测试服务器 `/json` 的固定JSON。真机使用电脑局域网IP，HTTPS另声明tls能力。

```sh
aster build apps/http-fetch --target xtensa
aster deploy dist/xtensa/aster.http.fetch.ast --target xtensa --port auto --run
```

详见[HTTP开发说明](../../docs/http.md)，当前尚未发布N2版本。
