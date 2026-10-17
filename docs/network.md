# APP 网络通信（ABI 3.8，工作区开发版）

公开TCP/UDP需要配套ABI3.8固件/模拟器；此前3.7 Release不具备这些导入。只需APP SDK进行开发，不依赖固件源码、Zephyr、ESP-IDF或芯片驱动。旧APP继续可用；引用新导入的APP必须设置`abi_min="3.8"`。当前工作区ABI3.11已包含TCP/UDP（最低ABI3.8）、HTTP/HTTPS（最低ABI3.9）及MQTT/MQTTS（最低ABI3.11）；见[HTTP开发说明](http.md)和[开发手册第18节](app-development.md#18-mqttmqttsabi-311-工作区开发版)。N1–N3已完成本阶段双板验收，N4复合验收范围与结果由对应固件验证记录维护；上述新增接口尚未发布Release。个人热点APP控制尚未提供。

## 工程与运行

`app.toml`设置`abi="3.8.0"`、`abi_min="3.8"`、`requires=["network"]`、`permissions=["net"]`。`wifi`权限只用于修改无线连接，不隐式授予网络通信。系统设置先联网，网络接口不会扫描、配网或重连路由器。

```bash
python tools/aster.py build apps/network-echo --target xtensa
python tools/aster.py verify apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa
python tools/aster.py deploy apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa --port auto --run
```

电脑运行`node tools/network_echo_server.cjs`。真机与电脑连接同一局域网，并把示例app.toml的`NETWORK_HOST`改为电脑的局域网IPv4。WSL NAT环境可以在Windows运行Node服务器、由WSL通过串口部署；连接目标必须是开发板能访问的Windows局域网地址。

模拟器保持127.0.0.1，运行`node src/cli.js APP.ast --network`，或Windows模拟器带相同参数。`--wifi`只模拟无线状态，不启用真实socket；`--network`启用宿主机实际IPv4连接。真实网络不能与确定性replay同时使用。模拟器复现两槽和数据队列等API限制，不模拟ESP32无线时延、信号、电源或两套堆占用。

## 接口规则

包含`<aster_imports.h>`即可使用`aster_network_get_info/open/send/recv/status/cancel/close`。完整签名、结构和示例见[开发手册第15节](app-development.md#15-tcpudp-网络通信abi-38)。所有操作非阻塞；在FRAME或TIMER事件轮询，EAGAIN留到后续事件重试，不能忙循环等待。

- `get_info`为只读快照，返回本机IPv4提示和连接/队列限额，不创建socket或扫描/联网。flags分别为PRESENT=1、UP=2、CARRIER=4、IPV4=8；UP仅代表接口启用，ESP载波标志可能在未关联时保留，不能据此证明关联或互联网可达。结构44字节，锁忙返回EAGAIN；模拟器地址是本机候选地址，不承诺与目标路由一致。
- IPv4单播，DNS域名或数字IPv4；host是1..253字节ASCII明确长度，不含NUL，端口1..65535。
- TCP打开异步返回正句柄；超时1..30000ms，涵盖DNS和连接。UDP打开使用空host、0长度、0端口、0超时，等READY再发包。
- 两个全局传输名额，TCP/UDP、HTTP/HTTPS、MQTT/MQTTS及CLI共用；HTTPS与MQTTS另共用一个TLS名额；APP不能读取或关闭其他会话/CLI句柄。每次启动创建新会话。关闭立即使句柄失效，后台完成回收前可能暂占名额，ENOSPC表示资源准入失败。
- 单次IO最多2048字节，UDP最多1200字节，收发队列各两包。TCP返回入队字节数，必须继续发送未接受后缀；UDP原子排队，满队列EAGAIN。
- 发送不是送达确认。TCP无进展5秒超时；UDP自入队起5秒（含DNS）。示例对二进制回包使用轮次号、每阶段最多3次超时重试；空包不能携带唯一轮次，不作为可靠消息ACK。自己设计消息边界、序号及重试规则，避免对不可重试操作盲目重复。
- 收取UDP容量不足返回EMSGSIZE，packet.length给出所需长度，原包保留。packet输出必须有效且与数据区不重叠，先验证全部输出再消费数据。空包是合法长度0，不是断开；无包为EAGAIN。
- TCP已缓存数据先于EOF/取消/错误返回；读到0是EOF，此后不能继续发送（EPIPE），本版不承诺半关闭后继续写。
- cancel丢弃未发输出、保留已收输入与终态；close立即失效，APP退出、初始化失败或trap时自动对整个会话执行关闭。无需为清理申请wifi权限。暂停时连接保持活动，应用应考虑暂停期间有限RX队列可能满。
- 输出结构必须传完整v1大小；更大size允许但只写v1前缀，尾部不改；size/version为输出值，不要求预填。invalid/foreign handle为EBADF，短结构/越界/溢出/重叠输出为EINVAL；不会把Zephyr errno泄漏给APP。

新增稳定错误：ECANCELLED=-19、EMSGSIZE=-20、EPIPE=-21、EDNS=-22、ECONNRESET=-23、ECONNREFUSED=-24、ENETUNREACH=-25。连接超时仍为ETIMEDOUT=-8，队列/锁忙重试为EAGAIN=-15。状态字段phase区分socket、DNS、connect、TX、RX、cancel。不要用负数绝对值解释POSIX errno，也不要用发送入队量推断实际回包量。

TCP服务私有缓冲每槽4KiB PSRAM，UDP每槽6568字节（Xtensa构建），另有常驻4KiB内部RAM线程栈；底层协议栈还占系统内存。不要按模拟器进程占用推算硬件APP可用内存，也不要将这些数字当作未来TLS预算。

当前两板固件另有共享的底层协议固定池：数据分片256字节，RX24片、TX32片，net_pkt描述符RX/TX各8个。两板ELF中的数据与net_buf数组合计15904字节PSRAM；这部分由系统与APP共用，不能算成每个APP的私有配额。两连接准入不等于系统协议资源永远充足，APP仍需处理底层资源不足和UDP有限重试。诊断采样器不包含在日常固件中。
