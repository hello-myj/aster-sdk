# HTTP Resume（N5.4 参考示例）

演示"前台下载与续传"的正确姿势，**固件协议栈与 ABI 不需要任何改动**（`abi_min="3.9"`，只用既有 `aster_http_*` + `aster_fs_*`）：下载到 `/data/file.part`，传输中断后用 `Range: bytes=N-` + `If-Range: <ETag|Last-Modified>` 续传；服务器回 200 或校验子不匹配时**从头重下、绝不拼接不同版本**；提交前把完整 temp 从头再校验一遍并逐块复制到最终名（公开 API 无 rename，复制+unlink 是如实形态），成功后才删 temp。取消/退出保留 temp 供下次续传；每帧一次读 + 一次写，点按暂停/继续，画面全程响应。

## 服务器契约（fixture：固件仓库 `tools/validation/network_resume_server.cjs`）

- 206 + `Content-Range: bytes S-E/T`，仅当 `If-Range` 匹配且 `S` 等于磁盘大小时
- 若 `If-Range` 不匹配、`Range` 被忽略或要求超界：200 全量（客户端必须重开）
- temp 已完整时 `416` + `bytes */T`

实现纪律（示例注释同步说明）：

1. 无校验子时**主动丢弃 temp**——无法证明前缀属于当前版本。
2. 响应头值不自动补 NUL（ABI 语义），超过缓冲区容量视为不可用。
3. 释放传输名额必须 close 不能 cancel（cancel 的终态句柄保留配额直到 close）。
4. `pattern` 或长度失配视为版本翻转：整文件重下，不修补。

## 运行

```bash
# 固件仓库内启动 Range-aware fixture（127.0.0.1:9082）
node tools/validation/network_resume_server.cjs
# SDK 构建（默认 HTTP_URL 指向 /resume?n=65536&cut=20000，强制真实续传）
python tools/build.py apps/http-resume --target xtensa --aot
aster deploy dist/xtensa/aster.http.resume.ast --port PORT
```

模拟器集成：`ASTER_HTTP_RESUME_AST=<.ast> ASTER_RESUME_SERVER=<server.cjs> node --test test/http.test.js` 端到端验证"断流→206 续传→校验提交→temp 清理"（Windows/Linux 均 96 项零跳过实跑）。实机双板验证待用户在场时执行（需实体屏确认），完成前本项按"实施中"记录。

把真实下载换成自己的 URL/路径时，请保留以上四条纪律，尤其是"If-Range 必须发、200 必重开、提交前必复验"。OTA/固件升级不在本示例范围（另行评审分区/签名/回滚）。
