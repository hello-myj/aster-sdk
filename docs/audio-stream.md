# 压缩音频流（ABI 3.10 工作区开发版）

`aster_audio_stream_*` 接受分段 MP3，收到足够数据即按 MP3 原采样率输出到立体声 I²S（8–48 kHz 标准 MP3 采样率），不需要文件、整首下载或整首内存副本。现有文件播放接口保持不变。网络读取由 APP 自己推进，固件音频线程不执行 HTTP。

工程设置：`abi="3.10.0"`、`abi_min="3.10"`、`requires=["audio-out","audio-stream"]`。HTTP 来源另需 `http`、HTTPS 另需 `tls`，权限 `net`；仅向音频流写字节不需要 `fs` 或 `net` 权限。

```c
int32_t aster_audio_stream_open(uint32_t codec); // ASTER_AUDIO_STREAM_MP3
int32_t aster_audio_stream_write(uint32_t handle, const void *data, uint32_t len);
int32_t aster_audio_stream_finish(uint32_t handle);
int32_t aster_audio_stream_status(uint32_t handle,
    aster_audio_stream_status_t *status, uint32_t size);
int32_t aster_audio_stream_close(uint32_t handle);
```

open 返回正数句柄；其他返回负数为 SDK 稳定错误码。全局只有一个音频会话，文件播放、录音或启动音效占用时返回 EBUSY，等待后续帧重试。每次 write 长度 1–2048 字节，返回实际接受长度，部分接受时保留未接受的尾部；EAGAIN 表示本次未接收。不能先读下一块 HTTP 数据覆盖尾部。

每会话分配 32 KiB PSRAM 输入环和 16 KiB PSRAM 解码窗口；另复用已有解码器、PCM FIFO、I²S 缓冲和播放线程。这里的 48 KiB 不是整个网络播放功能的总内存，HTTP/TLS、APP 线性内存和线程另计。约 24 KiB 预缓冲后开始输出；不足时重新缓冲，`underruns` 记录次数。大 ID3 标签以固定窗口跳过，超过 1 MiB 拒绝。

状态结构 56 字节：`uint32_t size,version,state; int32_t error; uint32_t queued,capacity,underruns,flags; uint64_t received,consumed; uint32_t played_ms,reserved;`。

- state：BUFFERING=1、PLAYING=2、PAUSED=3、DONE=4、FAILED=5。
- queued / capacity：32 KiB 输入环中的当前字节 / 容量，不包含解码窗口。
- received / consumed：累计接受的 MP3 字节 / 从解码窗口消耗的字节（包括跳过的标签）。
- played_ms：已送入 PCM 输出流程的音频时长；不是扬声器物理发声时间的精确测量。
- flags：`ASTER_AUDIO_STREAM_F_EOF` 表示调用过 finish；只有 DONE 才代表解码和输出流程结束。

HTTP read 返回 0 后调用 finish，等状态 DONE，再 close；HTTP 失败则 close，不把失败伪装成正常 EOF。finish 可重复调用，随后 write 返回 EPIPE。close 非阻塞地使句柄失效，异步丢弃尚未播放的数据，短暂 EBUSY 属于旧会话正在回收。终态句柄仍需 close。所有权绑定当前 APP，其他 APP 或失效句柄返回 EBADF；APP 退出、初始化失败和 trap 自动关闭。

现有 `aster_audio_pause/resume/stop` 对流生效；seek 不支持，返回 ENOTSUP。暂停不会暂停 HTTP 的总超时计时；下载最终受有界缓冲背压约束。不能假设任何公网线路都足以持续播放：128 kbps 至少需约 16 kB/s、320 kbps 至少需约 40 kB/s，还需余量吸收抖动。

完整事件驱动代码见 `apps/online-music/src/main.c`，包含部分写入、取消、切歌、HTTP/音频句柄回收和有界 JSON 解析。当前模拟器未提供流式音频能力，不应宣告 `audio-stream`；此示例需匹配的实机固件。

音质优化固件无需修改 ABI 或重编 APP。`aster_audio_get_info` 仍报告原生 PCM／录音／音效资源格式（当前16kHz）；不能把播放中歌曲的瞬时硬件采样率作为下一次原生PCM输入格式。MP3按源采样率输出，避免先降到16kHz；暂停恢复保留该时钟，下一次PCM或录音恢复原生格式。单条MP3流中途改变采样率返回EPROTO。100音量上限对应codec0dB，取消之前的正增益，80及以下映射保持不变。
