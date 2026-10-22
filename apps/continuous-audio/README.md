# Continuous Audio

当前 ABI 为 4.1.0。测试素材由应用生成，以 5 FPS 绘制，后台只启动一次循环音效。左侧点按平滑切换音高，右侧淡出停止/重启。暂停由固件生命周期自动冻结相位。

构建：`python tools/build.py apps/continuous-audio --target xtensa`。

完整参数、资源限制和未完成项见 [连续音效说明](../../docs/continuous-audio.md)。无窗口模拟器通过不代表真实 I²S 输出连续。
