# HTTP Download

ABI 3.9 工作区示例：每帧最多读取512B并写入APP沙箱 `/data/download.bin`，下载64KiB受控模式数据，逐字节校验并输出CRC32。响应超过内部队列，APP不分配整个响应。

修改 `app.toml` 的 `HTTP_URL` 为测试服务器 `/fixed?n=65536`；业务端点也需更换本例固定数据校验。HTTPS另声明tls能力。

```sh
aster build apps/http-download --target xtensa
aster deploy dist/xtensa/aster.http.download.ast --target xtensa --port auto --run
```

普通失败/退出删除部分文件，突然断电不保证事务性。详见[HTTP开发说明](../../docs/http.md)，当前尚未发布N2版本。
