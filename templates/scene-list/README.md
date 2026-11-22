# Scene 列表模板

模板清单要求最低 ABI 4.1 和 `display`、`touch`，初始化时还会查询可选 Scene 绘图能力。

```text
aster init my-settings --template scene-list --id com.example.settings --target xtensa
cd my-settings
aster build
aster run --port <串口>
```

包含 10 行本地设置、滚动、按钮捕获和生命周期取消。开关只改变本 APP 的演示状态，不更改系统设置。
公开接口、内存边界、资源所有权和返回值见 SDK 开发手册第 13 节。
