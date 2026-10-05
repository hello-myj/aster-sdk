# 单串口日志、命令与文件传输

新固件在 shell 串口增加 ASTERPING / ASTERPUT。LCKFB 的 CH340（本机 `/dev/ttyUSB0`、Windows COM24）即可查看日志、操作 shell、传文件、安装与运行 APP。微雪 `usb-debug.conf` 模式的原生 USB（`/dev/ttyACM0`）也支持同一接口，不再暂停文件服务。微雪新模式本次通过编译，尚未实机验证。

配套 SDK 探测到 `PONG ... serial=shared` 后自动共用该端口：

```sh
python tools/windows_device.py deploy dist/lckfb-s3/aster.imucube.ast --target lckfb-s3 --port /dev/ttyUSB0 --run
```

Windows 可使用 `aster deploy ... --target lckfb-s3 --port COM24 --run`。无需另指定 `--console`；旧固件仍可显式指定不同的命令串口。部署的传输、安装、启动复用一次串口连接。CH340 打开时保留 OS 的 DTR/RTS 同时有效状态，避免逐个撤销控制线触发 ESP32 重启；原生 USB 则保持两者无效。

一个串口接收流由 shell 独占。收到上传命令后，shell bypass 将二进制交给后台文件线程；日志继续经 shell 输出，协议回复用独立行隔开。结束、错误或连续 30 秒未收到数据后恢复命令解析。应用继续运行，文件写入不占用系统 workqueue。

协议仍为 ASTERFS 3：512 字节 stop-and-wait，写入临时 `.part`，整文件 CRC32 通过后发布。支持断点续传和拒绝非法路径。上传命令头在共享串口必须以**单个 LF**结束，不能使用 CRLF，避免剩余 LF 被算入二进制正文。正文可包含任何字节，包括 Ctrl+C、LF、NUL。配套 SDK 已使用 LF。

同一时间只能有一个文件上传。独立监视器与上传器不能同时读取该串口，上传前应关闭监视器，完成后打开同一端口恢复查看。应用开发者可用 `send_file(..., connection=connection)` 与 `shell_command(..., connection=connection)` 复用已经打开的 pyserial 连接；调用方必须统一接收，不能另起一个竞争的读取线程。

本次 LCKFB 实机检查：200 次 IMU 读取成功；立方体运行约 3 分钟、99 条周期采样日志无错误，用户确认随动、触摸正常；有 12 次首次零值重试提示，未出现驱动读取失败。共享串口上传 65797 字节二进制并通过 CRC；上传、安装、启动 68708 字节 IMU APP 成功；APP 运行时测试错误 CRC、非法路径、30 秒中断及从 512 字节处续传成功，随后 shell/IMU/退出均正常。详细日志位于工作区 `diagnostics/lckfb-shared-serial-20261005/`。
