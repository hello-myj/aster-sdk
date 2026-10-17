# HTTP/HTTPS 开发（ABI 3.9 工作区开发版）

N2已完成本阶段双板验收（2026-10-16），未发布为Release。APP 只使用 SDK，不安装 Zephyr 或 Espressif 驱动库。公共契约由固件导出 ZIP，再由 SDK 导入；不是各仓库手工维护独立签名。

## 工程与请求

工程的 `abi="3.9.0"`、`abi_min="3.9"`，`permissions=["net"]`，`requires=["http"]`；HTTPS 加入 `"tls"`，下载文件另加权限 `"fs"` 和能力 `"filesystem"`。网络访问不自动获得 Wi-Fi 设置权限，先通过系统设置联网。

使用 `<aster_imports.h>`，六个公开函数为 `aster_http_request/status/read/header/cancel/close`，完整签名见开发手册第16节与 `include/aster/http.h`。request 返回正句柄只代表已接收任务；状态和内容校验才能证明请求成功。

```c
static int32_t request;
static uint8_t buffer[512];
/* 在 FRAME/TIMER 事件推进；不要 while 等待网络。 */
static void poll_http(void)
{
    if (!request) {
        const char url[] = "https://your-server.example/data";
        int rc = aster_http_request(ASTER_HTTP_METHOD_GET, url, sizeof(url)-1,
            NULL, 0, NULL, 0, 30000, 65536);
        if (rc == ASTER_EAGAIN) return;
        if (rc < 0) { /* 报告错误 */ return; }
        request = rc;
    }
    aster_http_status_t status;
    int rc = aster_http_status(request, &status, sizeof(status));
    if (rc == ASTER_EAGAIN) return;
    if (rc < 0) { /* 报告错误 */ return; }
    rc = aster_http_read(request, buffer, sizeof(buffer));
    if (rc > 0) { /* 处理这 rc 个字节，不能假定以 NUL 结束 */ }
    else if (rc == ASTER_EAGAIN) return;
    else { /* 0=成功EOF；负值=错误。close也可能EAGAIN，下一事件重试。 */ }
}
```

## 资源与语义

- HTTP/HTTPS、TCP/UDP、MQTT/MQTTS 与诊断 CLI 共用两个传输名额；HTTPS 和 MQTTS 共用一个 TLS 名额。完成、失败、取消的句柄仍占名额，必须关闭。close 立即使句柄失效，后台收尾可能暂时保留名额；APP 停止、trap、初始化失败也会关闭其所有请求。
- 请求体仅 POST 可用，复制上限 2048 B；URL 1024 B，头部 2048 B/32字段，响应队列 2048 B，单次读取 1..2048 B。示例使用 512 B 缓冲。总流量上限最多64 MiB，不按此大小分配内存。
- 超时1..600000ms，包含 DNS、连接、握手和 APP 停读造成的背压；系统面板遮挡 APP 时网络仍在后台运行，但期限继续计时。关闭面板后继续消费，不能承诺任意长暂停仍成功。
- read 先返回缓冲数据，再报告 EOF、取消或错误。只有0代表成功EOF；EAGAIN不是EOF。HTTP 404/500仍是成功传输，APP检查status决定业务是否接受。
- header 按名称及序号查找重复字段；不自动拼接。值不带NUL。容量不足返回EMSGSIZE并写required，且不写部分值；required不能和输出重叠。响应头值目前只接受ASCII。
- 最多三次重定向，跨origin移除全部自定义头，拒绝HTTPS降级HTTP。301/302 POST改GET，303除HEAD外改GET，307/308保留有界请求体。不自动保存Cookie、解压、重试或复用连接。
- TLS验证证书链、名称和时间，缺少已同步时钟返回ECLOCK。默认根仅ISRG Root X1；不保证所有公共站点或证书链都可用。CA只由设备管理员/开发环境设置，APP不能更换全局根或跳过验证。
- EPROTO为非法/截断响应，ETLS为握手或记录失败，ECERT为证书拒绝，ECLOCK为未同步时钟，EREDIRECT为重定向次数超限。其他错误保持既有网络错误域。certificate_flags不是平台Mbed TLS原始标志。

## 示例与模拟器

`apps/http-fetch` 从受控 `/json` 获取小JSON，验证Content-Type及示例内容后显示；它没有实现通用JSON解析器。`apps/http-download` 从 `/fixed?n=65536` 以512B缓冲下载到APP沙箱 `/data/download.bin`，逐字节校验模式并输出CRC32。两者都是FRAME事件推进，保持动画，不在渲染线程等待网络。

```sh
aster build apps/http-fetch --target xtensa
aster build apps/http-download --target xtensa
```

默认URL使用模拟器的127.0.0.1。测试服务器是固件开发仓库 `tools/validation/network_http_server.cjs`（仅测试工具，不是SDK依赖）；实际产品改为自己的服务。真机将 `app.toml` 的 `HTTP_URL` 改成电脑局域网地址，再构建部署；HTTPS改用9443并声明tls能力。fixture是固定测试数据，换业务端点时也要更换应用层校验逻辑。

```sh
node src/cli.js /path/to/aster.http.fetch.ast --network
node src/cli.js /path/to/aster.http.download.ast --network
```

上述模拟器命令在模拟器目录执行。测试私有CA时可追加 `--network-ca /path/to/ca.pem`，仍强制验证；默认根与设备一致。不与确定性replay同时使用。Node自身的socket/TLS缓冲不等于设备PSRAM模型，模拟器仅保证公开队列有界，设备内存峰值以实测为准。

下载示例在普通失败/退出时删除未完成文件；没有公开rename/fsync事务接口，突然断电可能留下部分文件。本例不能当作断电安全的安装器或OTA实现，完成日志以成功EOF、长度/内容校验及文件关闭成功为前提。
