# Scene 列表模板

使用带 G7 Scene 能力的固件/模拟器；仅 ABI 3.7 版本号不足以判断，初始化会查询可选能力。

```text
aster init my-settings --template scene-list --id com.example.settings --target xtensa
cd my-settings
aster build
aster run --port <串口>
```

包含 10 行本地设置、滚动、按钮捕获和生命周期取消。开关只改变本 APP 的演示状态，不更改系统设置。
公开接口、内存边界、资源所有权和返回值见 SDK 开发手册第 13 节。
