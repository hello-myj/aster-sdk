# 在线音乐

边下载边播放 MP3，不把整首歌存成文件。需要 `audio-stream`、`http` 和 `net`。模拟器不提供 `audio-stream`，这个示例在设备上运行。

清单中的应用 ID 为 `aster.music.online`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`、`http`、`tls`、`audio-out`、`audio-stream`。

```bash
python tools/build.py apps/online-music --target xtensa --aot
aster deploy apps/online-music/dist/xtensa/aster.music.online.ast --target xtensa --port <串口> --run
```

打开后点列表播放。切歌会先关掉当前网络和音频句柄。曲目 ID 由编译期的 `MUSIC_IDS` 决定。
