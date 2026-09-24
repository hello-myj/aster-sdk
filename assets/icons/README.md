# App 图标（ASTI）

每个 App 包根下的 `icon.bin` 是**已生成并入库**的产物，不是构建期生成的：
`build.py` 只校验它是 ASTI、塞进 `.ast`，从不自己渲染。所以换图标要手动重跑，
下面是可复现的对应表和命令。

图标是**全彩 96×96 瓦片**：一块该 App 自己的渐变色圆角方块，上面压白色的
Lucide 字形。`app_icons.py` 把它们编成 **RGB565A8**（16 字节头 + 96×96×2 的
RGB565 平面 + 96×96×1 的 A8 平面 = **27664 字节**），launcher 原样贴上去，
不染色、不画底——所以一个 App 在五套主题下长得一模一样
（`firmware/ui/launcher/launcher.c`）。圆角外的像素 alpha=0，露出壁纸。

上游矢量在 `lucide/`（Lucide ISC，锁定 lucide-static v1.41.0；见 `lucide/NOTICE`），
源文件是 **24×24 矢量**，由 `app_icons.py` 在 96 上直接重绘：24 单位的字形
缩放进 54 px 的方框，描边 `stroke-width="2.4"` 放大后约 5.4 px。

## 生成

```bash
python3 sdk/tools/app_icons.py                 # 全部
python3 sdk/tools/app_icons.py player          # 只重做某几个
python3 sdk/tools/app_icons.py --sheet /tmp/icons.png   # 另外出一张九宫格预览
python3 sdk/tools/app_icons.py --check         # 只比对，不写；文件过期则退出码 1
```

颜色表、瓦片尺寸、圆角、字形方框全都写在 `app_icons.py` 顶部，改完重跑即可。
**唯一需要动固件的情况是改 `TILE`**：launcher 的 `LAUNCHER_WELL_PX` 要和它一致，
否则图标会被缩小或留空。改配色、换字形、调圆角都不需要碰固件。

| App | 源文件 | 颜色 |
|-----|--------|------|
| `aster.apitest` | `lucide/cpu.svg` | 青 `#0891B2 → #22D3EE` |
| `aster.gallery` | `lucide/image.svg` | 紫 `#7C3AED → #A855F7` |
| `aster.game.2048` | `lucide/layout-grid.svg` | 橙 `#EA580C → #F59E0B` |
| `aster.gfxshow` | `lucide/palette.svg` | 品红 `#C026D3 → #E879F9` |
| `aster.permdeny` | `lucide/lock.svg` | 红 `#DC2626 → #EF4444` |
| `aster.player` | `lucide/music.svg` | 靛蓝 `#4F46E5 → #818CF8` |
| `aster.reader` | `lucide/book-open.svg` | 翠绿 `#047857 → #10B981` |
| `aster.snake` | `lucide/gamepad-2.svg` | 黄绿 `#4D7C0F → #84CC16` |
| `aster.trap` | `lucide/alert.svg` | 琥珀 `#CA8A04 → #FACC15` |

色相绕轮均匀铺开，同屏不会有两只图标撞色；`2048` 的橙和 `trap` 的琥珀最近
（约 20°），靠明度拉开。

产物应是 **27664 字节**，且**可复现**——同源同参数重跑一次字节一致（`--check`
就是这条契约的自动检查）。需要 `pip install cairosvg pillow`，仓库 venv 里已有。

新增 App 时：挑一个语义贴切的 SVG（`lucide/` 里没有就按 `NOTICE` 的 URL 取同版本）
→ 在 `app_icons.py` 的 `ART` 表里加一行（目录名、SVG 名、两个色标）→ 跑一遍 →
在 `app.toml` 里加 `icon = "icon.bin"` → 把新的一行补进上表。

## 其它格式

`icon_convert.py` 还支持 `--format a8|rgb565|i1`，但**不要用它处理全彩稿**：
它的 SVG 分支是先在黑底上渲染、再拿亮度当 alpha，那是给单色线稿掩膜写的，
用在彩色稿上会让所有深色像素变透明。彩色图标走 `app_icons.py`。

画布/launcher chrome 用的仍是 24×24 光栅化的 A8 掩膜，由
`sdk/tools/launcher_font_gen.py` 烤进 rodata（那些**应该**跟主题走）。

抽屉仍然认 A8：老包（launcher 改全彩之前打的）会退化成「中性卡片底 + 主题前景色
字形」，画得出来但不再是 App 自己的颜色。重打一遍包就升级到全彩。
