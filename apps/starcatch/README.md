# Star Catch

拖动接住黄星，避开红陨石。漏接或碰到陨石扣一条命，三条命后结束。点按开始或重试。左右键也能移动。

清单中的应用 ID 为 `aster.starcatch`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`。

```text
aster build apps/starcatch --target xtensa --aot
aster deploy apps/starcatch/dist/xtensa/aster.starcatch.ast --target xtensa --port <串口> --run
```
