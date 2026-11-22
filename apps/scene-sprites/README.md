# Scene 精灵

在一页里摆放多张精灵，滚动时重建可见区域。

清单中的应用 ID 为 `aster.scene.sprites`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`、`filesystem`。

```text
aster build apps/scene-sprites --target xtensa --aot
aster deploy apps/scene-sprites/dist/xtensa/aster.scene.sprites.ast --target xtensa --port <串口> --run
```
