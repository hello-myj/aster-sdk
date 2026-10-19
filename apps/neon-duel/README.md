# 霓虹对决

横屏像素格斗。当前 ABI 为 4.0.0，CPU 目标 `xtensa`。240×320 面板旋转后使用 320×240 坐标，其他尺寸按 4:3 居中。

## 操作

- `FIGHT`：对战，90 秒一回合，先赢两回合获胜。
- `TRAIN`：陪练，KO 后双方回血。
- 底部 `<` / `>` 移动，`UP` 跳跃，`BLOCK` 防御，`PUNCH` / `KICK` / `BURST` 攻击。
- 点顶部计时器暂停。

模拟器里可用方向键或 A/D 移动，W 或空格跳跃，S 防御，Z/J 出拳，X/K 踢，C/L 必杀，P 暂停，Esc 返回。

## 构建

```text
aster build apps/neon-duel --target xtensa --aot
aster verify apps/neon-duel/dist/xtensa/com.example.neonduel.ast --target xtensa
aster deploy apps/neon-duel/dist/xtensa/com.example.neonduel.ast --target xtensa --port <串口> --run
```

模拟器运行同一个 `.ast`，执行其中的 WASM。点“启用声音”后可以听到音效。

主机规则测试：

```bash
gcc -DNEON_HOST_TEST -std=c99 -Wall -Wextra tests/combat.c src/game.c -o tests/combat-test
./tests/combat-test
```

在 `apps/neon-duel` 目录执行。资源由 `python tools/make_assets.py` 生成，普通构建直接使用已生成的文件。
