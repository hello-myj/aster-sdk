# IMU 立方体

基于 Aster SDK ABI 3.7 的 C/WASM/AOT APP，只调用公开 APP API，不依赖 Zephyr、LVGL 或板级驱动。

首次进入请将设备静置约两秒，自动估计陀螺仪零偏。随后转动设备，立方体按六轴融合姿态旋转；底部显示 SI 单位原始读数。“归零”将当前姿态设为显示参考，让立方体正面朝向屏幕；“校准”重新开始静置采样。校准期间移动会重新累计。

六轴没有磁力计，航向会长期漂移；强线性加速度时重力校正暂缓。坐标沿传感器轴，屏幕竖横方向不改变原始轴。此示例要求至少 200×300 的 APP 画布，默认竖屏。

显示映射将相对姿态四元数的 x/y 分量取反、保留 w/z，使 roll/pitch 与设备观察方向一致、yaw 保持原方向。该映射只用于立方体绘制，原始读数与姿态融合不变。

激活 SDK 环境后，在该目录运行：

```text
aster build --target xtensa
aster run --target xtensa --port SERIAL_PORT
```

也可从任意目录执行 `aster build /absolute/path/to/aster-sdk/apps/imucube --target xtensa`。包为本工程的 `dist/xtensa/aster.imucube.ast`。需要运行 ABI 3.7 或更新兼容固件，并实际启用 accelerometer 与 gyroscope 能力。`SERIAL_PORT` 替换为设备实际串口；当前微雪默认固件未启用 IMU，此 APP 会因缺少能力被拒绝安装或启动。

实现包括同步 IMU 采样、无 math 库依赖的四元数互补滤波、彩色面透视投影、完整帧重绘、触摸归零和校准、低频诊断日志。开发接口和辅助库的参数定义以 SDK 自包含开发手册为准。

图标为 96×96 ASTI RGB565A8；修改或重新生成用 `python tools/make_icon.py`，无需额外图像依赖。
