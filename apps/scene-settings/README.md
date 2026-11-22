# Scene 设置列表

十行本地设置。滚动松手不会当成点击。

清单中的应用 ID 为 `aster.scene.settings`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`。

```text
aster build apps/scene-settings --target xtensa --aot
aster deploy apps/scene-settings/dist/xtensa/aster.scene.settings.ast --target xtensa --port <串口> --run
```
