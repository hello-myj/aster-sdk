# Scene 设置列表

10 行本地演示设置；滚动后松手不触发点击。静态页仍轮询字体 epoch，但不发布重复帧。

```text
python tools/build.py apps/scene-settings --target xtensa --aot
python tools/deploy.py dist/xtensa/aster.scene.settings.ast --port <串口> --run
```

需要支持帧裁剪和字体 epoch 的 G7 固件/模拟器。旧 Host 初始化返回 `ASTER_ENOTSUP`，不会退回不完整绘制。
接口参考 [开发手册](../../docs/app-development.md#13-scene布局与控件g7)。日志含发布计数和错误计数，调试产物不属于发布资源。
