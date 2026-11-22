# Snake

滑动转向。死亡后轻触重新开始。移动、吃到食物和死亡各有短音效。

清单中的应用 ID 为 `aster.snake`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`。

```text
aster build apps/snake --target xtensa --aot
aster deploy apps/snake/dist/xtensa/aster.snake.ast --target xtensa --port <串口> --run
```

没有音频输出时游戏仍然运行。
