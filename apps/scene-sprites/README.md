# Scene 多精灵

六个精灵共用一份 APP 自行生成的 16×16 BMP，演示缩放、色键、透明度、容器裁剪、隐藏及删除/重建；点按暂停或继续。

```text
python tools/build.py apps/scene-sprites --target xtensa --aot
python tools/deploy.py dist/xtensa/aster.scene.sprites.ast --port <串口> --run
```

需要支持帧裁剪和字体 epoch 的 G7 固件/模拟器。旧 Host 初始化返回 `ASTER_ENOTSUP`，不会退回不完整绘制。
接口参考 [开发手册](../../docs/app-development.md#13-scene布局与控件g7)。日志含发布计数和错误计数，调试产物不属于发布资源。
