# IMU 立方体

用六轴数据转动立方体。进入后静置约两秒完成零偏估计。底部可归零或重新校准。当前 ABI 为 4.1.0。

```text
aster build --target xtensa --aot
aster run --target xtensa --port <串口>
```

设备需要同时提供加速度计和陀螺仪。微雪默认配置没有这两项，安装会被拒绝。
