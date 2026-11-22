# IMU 立方体

用六轴数据转动立方体。进入后静置约两秒完成零偏估计。底部可归零或重新校准。

清单中的应用 ID 为 `aster.imucube`，最低 ABI 为 `4.0`，启动所需能力为 `display`、`touch`、`accelerometer`、`gyroscope`。

```text
aster build --target xtensa --aot
aster run --target xtensa --port <串口>
```

设备需要同时提供加速度计和陀螺仪。微雪默认配置没有这两项，尝试启动时会报告缺少必需能力。
