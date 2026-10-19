# 2048

滑动合并数字。右上角分别开关背景音乐和音效，设置保存在应用 KV。达到 2048 后点屏幕继续，失败后点屏幕重开。

```text
aster build apps/game.2048 --target xtensa --aot
aster deploy apps/game.2048/dist/xtensa/aster.game.2048.ast --target xtensa --port <串口> --run
```

没有音频输出时游戏仍然运行。`audio/` 里的文件由 `make_audio.py` 生成，普通构建直接使用已提交的 WAV。
