# HTTP Resume

下载到 `/data/file.part`。中断后用 `Range` 和 `If-Range` 续传。服务器返回 200 或校验子不匹配时从头重下。提交前再核对整份临时文件，然后复制到最终文件名。当前 ABI 为 4.0.0。

```bash
node <固件仓库>/tools/validation/network_resume_server.cjs
python tools/build.py apps/http-resume --target xtensa --aot
aster deploy apps/http-resume/dist/xtensa/aster.http.resume.ast --target xtensa --port <串口> --run
```

测试服务在固件仓库，默认地址写在 `app.toml` 的 `HTTP_URL`。
