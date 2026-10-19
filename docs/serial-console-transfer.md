# 串口传输

当前固件在 shell 串口上同时提供日志、命令和文件传输。`aster probe` 看到 `serial=shared` 后，安装和启动使用同一个端口。

```text
aster deploy dist/xtensa/<应用>.ast --target xtensa --port <串口> --run
aster shell --console <串口> aster sys info
```

`--port` 填系统枚举到的串口。上传前关闭占用该口的监视器。

协议是 ASTERFS 3：512 字节停等式传输，先写入 `.part`，CRC32 通过后发布。命令头以单个 LF 结束。同一时间只能有一个上传。
