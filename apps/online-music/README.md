# 在线音乐

边下载边播放 MP3，不把整首歌存成文件。需要 `audio-stream`、`http` 和 `net`。当前 ABI 为 4.1.0。模拟器不提供 `audio-stream`，这个示例在设备上运行。

```bash
python tools/build.py apps/online-music --target xtensa --aot
aster deploy apps/online-music/dist/xtensa/aster.music.online.ast --target xtensa --port <串口> --run
```

打开后点列表播放。切歌会先关掉当前网络和音频句柄。曲目 ID 由编译期的 `MUSIC_IDS` 决定。
