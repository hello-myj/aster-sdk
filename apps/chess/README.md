# Chess

玩家执白，设备执黑。点自己的棋子，再点目标格走棋。`NEW` 开新局，`UNDO` 撤销一个回合。

```text
aster build apps/chess --target xtensa --aot
aster deploy apps/chess/dist/xtensa/aster.chess.ast --target xtensa --port <串口> --run
```
