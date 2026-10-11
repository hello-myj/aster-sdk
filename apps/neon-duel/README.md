# 霓虹对决 / Neon Duel

使用 Aster APP SDK ABI 3.7 的原创像素格斗游戏。与设备相同的 C/WASM 应用包直接在 aster-sim 运行，没有另写网页游戏。CPU 目标为 xtensa；240×320 面板旋转后采用 320×240 横屏坐标，其他尺寸按 4:3 等比显示并留黑边。

## 玩法

- `FIGHT`：对战 AI，90 秒一回合，先赢两回合获胜；超时比较血量，平局不计胜场。
- `TRAIN`：静止陪练，KO 自动恢复双方血量，便于测试动作和连击。
- 底部 `<` / `>` 长按移动；`UP` 跳跃，未按方向时向对手跳进；`BLOCK` 长按防御，地面面向攻击时减伤。
- `PUNCH` 拳击，伤害 9、距离 47；`KICK` 踢击，伤害 15、距离 64；`BURST` 消耗 60 能量，发出伤害 24 的能量弹。能量缓慢恢复，命中和受击也会增加能量。
- 攻击有前摇与恢复时间，每次攻击只造成一次伤害。跳跃可以闪避；格挡仍有少量损伤。攻击命中带硬直、击退、短暂停顿和火花。
- 点顶部计时器暂停；暂停后点屏幕继续；回合结束点屏幕下一回合；比赛结束点屏幕重赛。

硬件单点触摸可逐个操作，跳跃自动向前便于接近目标；应用处理 8 个触点 slot，但当前模拟器输入为单点触摸。真机多点数量以硬件为准。

模拟器点屏幕后可使用键盘：左右方向键或 A/D 移动，上/W/空格跳跃，下/S 防御，Z/J 出拳，X/K 踢腿，C/L 必杀，P/Enter 暂停，Esc 返回标题（标题下 Esc 退出）。浏览器失焦会释放按键。

## 构建、校验与运行

使用 ABI 3.7 的 SDK。新的 Windows 便携发布包可以直接构建并部署，不需要 WSL。先安装并激活 SDK 环境。

从 SDK 根目录执行：

```powershell
aster build apps/neon-duel --aot
aster verify apps/neon-duel/dist/xtensa/com.example.neonduel.ast --target xtensa
./apps/neon-duel/run-simulator.ps1
```

产物：`dist/xtensa/com.example.neonduel.ast`（相对本应用工程）。模拟器必须读取此 `.ast`，才能同时加载包中的音效。点模拟器的“启用声音”解除浏览器播放限制。

无窗口冒烟验证：

```powershell
./apps/neon-duel/run-simulator.ps1 -Headless -Frames 120
```

战斗规则测试：

```powershell
wsl bash -lc "cd '/mnt/e/个人项目资料/aster/aster-sdk/apps/neon-duel' && gcc -DNEON_HOST_TEST -std=c99 -Wall -Wextra tests/combat.c src/game.c -o tests/combat-test && ./tests/combat-test"
```

真机 AOT 包在匹配 ABI 3.7 的 SDK 中使用 `aster build <工程路径> --aot`，然后 verify 和 deploy；模拟器同样可以读取包含 AOT 的包，仍执行其中的 WASM。

2026-10-02 已实际部署到 COM22 文件服务 / COM24 控制台的 lckfb-s3 开发板。设备确认 ESP32-S3、240MHz 双核、8MiB 外部 RAM、16MiB Flash、固件 ABI 3.7.0 与 SD 已挂载。包安装成功，运行日志明确报告 `app started ... AOT`，应用坐标为 320×240；`tests/board-title.png` 是通过设备截屏接口读取的真实画面。随后观察到持续的显示刷新日志，无应用退出或异常报告。尚未测定完整战斗的真机帧率，也未自动验证物理触摸；触摸手感需在设备上操作确认。

1.0.1 修复真机无声：通过公开音频接口确认设备为 16000Hz / 双声道 / S16，原来的 44100Hz 音效在该设备加载返回 EINVAL。现在按查询结果选择 `assets/16000/` 或原来的 44100Hz 资源；每次加载和播放检查返回值，并在启动时播放提示音。修复版已通过 Windows 便携 SDK 构建并重新部署，真机日志确认音效加载 4/4、音量 100、AOT 启动成功。模拟器的 44100Hz 资源同样加载 4/4，120 帧冒烟运行通过。用户已在开发板现场确认可以听到声音。

启动时固件的 SPI 传输临时缓冲不足，自动将传输块缩小到 4096 字节，随后截屏和刷新正常。未修改 SDK 契约或固件。

## 实现与资源

`src/game.c` 是与运行时无关的定步长战斗逻辑；`src/main.c` 只通过手册公开的 SDK 接口处理显示、输入、音效和生命周期。模拟采用 33ms 步长，异常长帧最多追赶 5 步，暂停后重建时间基准。

`tools/make_assets.py` 用代码生成原创像素角色、屋顶场景、图标和合成立体声 PCM 音效，没有下载第三方资源或使用已有游戏 IP。资源原件 `assets/arena.png`、`assets/fighters.png` 与生成脚本保留在工程中，可自由修改用于此项目。`src/art.h` 是编译时 RGB565 场景与 RLE 角色帧，绘制时可直接镜像，避免每帧解码或加载大量精灵句柄。

重建资源：Python 安装 Pillow 后执行 `python tools/make_assets.py`，再重新 build。普通构建不需要 Python/Pillow，已包含生成结果。

RGB565 表面、320×240 工作帧和场景常量各约 150KiB；线性内存初始 768KiB，最大 2MiB，运行时 surface heap 384KiB，支持测试过的 320×240 和 480×320 横屏。更大的表面仍受设备内存预算限制。音效提供 16000Hz 和 44100Hz 两套双声道 S16 WAV，每个小于 64KiB；只加载当前设备对应的四个音效，无声音能力或加载失败时保持游戏可用。每帧重新 acquire 表面并按字节 pitch 写入，退出时幂等释放表面和音效。
