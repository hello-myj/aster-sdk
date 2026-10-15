# Aster APP SDK 开发手册（AI 可直接使用）

适用版本：SDK ABI **3.8.0**；应用目标是 CPU 平台 **xtensa**（工具链目录目前只能编译 Xtensa，因此不提供 riscv/arm 目标文件）。契约基线为固件导出的 `aster-contract-3.8.0.zip`（工作区开发版），更新日期 2026-10-15。具体开发板由固件选择，SDK 不提供 `--board`。

本文是独立开发所需的完整参考：创建工程、配置、生命周期、绘图、输入、资源、设备接口、构建、部署及排错。本文中的类型和函数签名来自此版本 SDK 的公开契约。阅读本文即可编写 APP；不需要再读取固件代码、SDK 头文件、模板代码或工具实现。编译器会自动使用 SDK 自带的头文件和辅助库。

“只读手册”指开发知识的来源；仍需能创建文件、执行构建命令和查看构建/设备日志。命令执行不等于读取工具源码。本文不能替代本机 SDK 安装包、编译器和设备固件。不要将本文用于不同 ABI 的 SDK；升级 SDK 时应同步升级手册。

## 目录

- [1. 给 AI 的开发约定](#1-给-ai-的开发约定)
- [2. 安装与命令](#2-安装与命令)
- [3. 工程和配置](#3-工程和配置)
- [4. 完整可构建示例](#4-完整可构建示例)
- [5. 生命周期、事件和时间](#5-生命周期事件和时间)
- [6. 绘图、字体和布局](#6-绘图字体和布局)
- [7. 文件、资源与持久化](#7-文件资源与持久化)
- [8. 音频、图片、传感器与相机](#8-音频图片传感器与相机)
- [9. 系统能力与其他接口](#9-系统能力与其他接口)
- [10. 构建、部署与排错](#10-构建部署与排错)
- [11. SDK C 辅助库参考](#11-sdk-c-辅助库参考)
- [12. 完整公开 ABI 参考](#12-完整公开-abi-参考)
- [13. Scene、布局与控件（G7）](#13-scene布局与控件g7)
- [14. 字体与图片质量（G8）](#14-字体与图片质量g8)

## 1. 给 AI 的开发约定

接到需求后，先确定 SDK 所在绝对路径、工程输出目录、APP ID、交互和资源需求。目标未指定时使用 `xtensa`，APP ID 使用 `com.example.<英文名称>`；用户已有工程时保留其 ID 和业务代码。SDK 路径无法从工作环境确定时只询问路径，不要求用户查代码。

开发顺序：

1. 使用第 2 节命令检查安装，创建工程，或按第 3、4 节直接创建文件。
2. 编写 `app.toml` 与 `src/*.c`，只使用本文列出的公开接口和 SDK C 辅助库。
3. 实现 `aster_app_init` 与 `aster_app_event`；检查初始化错误，正确释放资源。
4. 根据实际 `app_width/app_height` 布局；处理触摸、按键和字体完成后的重绘。
5. 执行 `aster build` 和 `aster verify`，根据错误修正到成功。
6. 用户需要真机运行时执行 `aster run`，查看日志与截图；没有设备时明确只完成了构建验证。

APP 是 freestanding **C → WASM/AOT** 模块，不是 Zephyr 工程，没有 `main()`、线程入口或永久循环。使用 `<aster_imports.h>` 引入公开 ABI、精简 libc 和日志；使用 `<aster_gfx_engine.h>` 引入绘图与文字辅助函数。SDK 自动链接其 CRT、日志格式化和基础绘图库，不需要手写 Makefile/CMakeLists。

不要引用 Zephyr、ESP-IDF、LVGL 或固件内部头文件；不要照普通桌面 C 程序使用 `stdio`、POSIX 文件/socket、pthread、WASI、`sleep`。仅使用本文明确提供的函数；缺少某个能力时说明限制，不编造 API。

可交给 AI 的提示词：

> 只读取 Aster APP SDK 开发手册作为开发参考。SDK 位于【绝对路径】，工程创建在【绝对路径】，目标 xtensa，APP ID【ID】。需求是【功能与交互】。按手册创建 app.toml 和 C 代码，执行构建及包验证。不要读取 SDK/固件源码或工具实现，不要修改 SDK 契约；遇到缺少的接口直接说明。真机部署【需要/不需要】。部署时给出串口 `--port`，不要从板号推断端口或屏幕尺寸。

## 2. 安装与命令

### Linux x86_64

当前 Linux 便携发布包需要 Python 3.12，自带 Python 依赖、WASM 编译器与 Xtensa AOT 编译器。完整解压后，在 SDK 根目录执行：

```bash
cd /absolute/path/to/aster-sdk
./aster install
source ./env.sh
aster doctor
aster targets
aster templates
```

便携包的 `install` 生成安装位置对应的环境文件，不创建 venv 或下载依赖。每次打开新 shell，执行 `source /absolute/path/to/aster-sdk/env.sh`。可选 `./aster install --add-path` 持久配置 PATH。若使用 SDK 源码仓库，则需要 Python 3.11+，执行 `./bin/aster install` 创建 SDK 私有 venv 并安装依赖，然后同样 source 环境文件。

### Windows 原生发布包

完整解压并保留整个便携 SDK 目录，包括三个 EXE、`runtime/`、`tools/`、`toolchain/`、公开契约、模板、手册和 Skill；不能只复制 EXE。普通开发者无需安装 Python、WSL 或 Visual Studio。

PowerShell：

```powershell
cd C:\absolute\path\to\aster-sdk
.\aster.exe install
. .\env.ps1
aster doctor
aster targets
aster templates
```

CMD 激活用 `call C:\absolute\path\to\aster-sdk\env.cmd`。可选 `aster.exe install --add-path` 持久写入用户 PATH；移动 SDK 后重新生成环境文件。使用 Windows 发布包里的目录作为 SDK 路径，不要以 Linux 工具链代替。

### 创建与构建

工程可在 SDK 之外；下面的命令在项目的父目录执行：

```text
aster init my-counter --template hello --id com.example.counter --name "Counter" --target xtensa
cd my-counter
```

目的目录必须不存在。`hello` 是最小模板；`swipe-game` 是带滑动与音效的游戏模板。生成后用第 4 节完整替换 `app.toml`、`src/main.c`，无需查看模板内容。也可直接创建第 3 节的目录和这两个文件。

```text
aster build
aster verify dist/xtensa/com.example.counter.ast --target xtensa
```

构建默认遵循 `app.toml` 的 `aot`，可用 `aster build --aot` 强制 AOT，或 `aster build --no-aot` 为模拟器/主机测试生成 WASM 包。真实设备只接受并运行匹配 CPU 的 AOT；不支持 WASM 解释执行或自动回退。指定其他目标用 `--target TARGET`；目标选择优先级为命令行、项目 `[sdk].target`、`ASTER_TARGET` 环境变量、唯一已安装目标。

## 3. 工程和配置

最小图形工程：

```text
my-counter/
  app.toml
  src/
    main.c
```

有资源或多文件代码时可扩展：

```text
  src/game.c
  src/game.h
  images/background.png
  audio/click.wav
  icon.bin
```

构建器自动扫描 `src/` 下所有 `.c`；也可用 `[app].sources` 明确列出。资源路径相对工程根目录。`build/`、`dist/` 是生成目录，不能保存业务源码或唯一的资源原件。

以下字段全部位于 `[app]`，除非特别注明：

- `id`：必填，1–63 个 ASCII 字符，首字符是字母或数字，其余允许字母、数字、下划线、点、连字符，不含 `..`。建议反向域名形式。
- `name`：显示名称，默认 ID。使用 UTF-8，建议短名称。
- `description`：可选的 APP 简介，launcher 主页从安装包读取。单行 UTF-8 字符串，最多 127 字节（约 42 个汉字）；建议简短。缺省或空字符串时介绍区域留空。修改后重新构建并安装 `.ast` 包即可更新，无需修改 launcher。旧固件可能忽略此字段。
- `version`：APP 自身版本字符串，默认 `"0.0.0"`，与 ABI 无关。
- `abi`：编译目标 ABI，当前填 `"3.8.0"`；省略时使用 SDK 契约版本。
- `abi_min`：最低固件 ABI，当前新工程填 `"3.8"`。不要为了兼容旧设备而降低它却继续引用新接口。它的主版本必须与 `abi` 相同。
- `aot`：真实设备必须为 `true`，包同时包含 WASM 与目标 CPU 的 AOT；缺少 AOT 编译器会报错。模拟器可使用 `false` / `--no-aot`。设备会拒绝缺少 AOT、CPU 不匹配或 AOT 加载失败的 APP，不会自动回退 WASM。AOT 不是跨 CPU 通用二进制。
- `optimize`：`"O0"`、`"O1"`、`"O2"`、`"O3"`、`"Os"` 或 `"Oz"`，默认 `"O2"`；设备 APP 推荐 `"Os"`。
- `orientation`：`"portrait"` 或 `"landscape"`，在初始化前应用。当前设备普通 APP 默认竖屏；需要横屏时明确声明，运行时仍必须查询坐标空间。
- `category`：`"game"`、`"tool"`、`"system"`，不填时没有分类。
- `stack`：字节，默认 16384；既用于链接时的栈空间，也写入运行时内存配置。避免大数组放在函数局部栈上。
- `initial_memory`：WASM 初始线性内存字节，默认 262144。
- `max_memory`：可选，WASM 最大线性内存字节；未填不显式设置链接器最大值。初始值和最大值按 65536 字节页对齐，最大值不小于初始值。
- `heap`：可选，运行时 APP 堆预算字节，供运行时分配表面等对象；省略/0 时当前固件使用 262144。它不代表 SDK 提供了标准 `malloc/free` 的开发约定。
- `requires`：必需能力字符串数组，缺失时构建/安装可能被拒绝。默认空。
- `optional`：可选能力字符串数组，代码必须检测并降级。默认空。
- `permissions`：权限字符串数组，默认空，具体规则如下。
- `sources`：可选，工程相对路径数组；省略时扫描 `src/**/*.c`。
- `assets`：可选，需要打包的每个资源的工程相对路径；不支持通配符。默认空。
- `icon`：可选，工程内 ASTI 格式图标的相对路径，例如 `"icon.bin"`；不能直接填 PNG。
- `entry`：可选包元数据。普通 C APP 不设置，生命周期函数仍使用固定名称。

支持的能力名称（精确拼写）：`display`、`touch`、`mouse`、`keyboard`、`camera`、`audio-in`、`audio-out`、`wifi`、`bluetooth`、`accelerometer`、`gyroscope`、`filesystem`、`storage.app`、`rtc`、`psram`、`framebuffer`、`network`。名字被接受不代表当前这块板具备该能力。打包只要求 `target_id` 等于平台名 `xtensa`。加速度计、屏幕尺寸等差异在设备上用 `aster_system_has_feature` 和 `aster_display_get_info` 判断，安装不会因为这些板级能力拒绝包。

支持的权限名称只有：

- `"fs"`：应用文件、打包资源、共享音乐，以及图片/音频文件加载。
- `"kv"`：APP 独立持久化配置。
- `"sd"`：访问 `/sd` 共享 SD 根目录，文件操作还需 `"fs"`。
- `"appmgr"`：应用列表、启动其他 APP 和卸载。
- `"wifi"`：公开 Wi-Fi station 控制接口。
- `"net"`：TCP/UDP网络通信，不授予Wi-Fi配置权限。

例如读图片需要 `permissions = ["fs"]`；有文件系统能力不等于有文件权限。显示、触摸、传感器、相机、录音、音量控制通过硬件能力检测，不存在 `"display"`、`"camera"`、`"audio"` 权限。

`[sdk]`：`target = "xtensa"`，用于独立工程默认平台。`cpu` 与 `aot_target` 都是这个平台名。

`[build]` 可选字段：`source_roots = ["vendor/lib"]` 额外递归编译的 C 源目录；`includes = ["src", "vendor/lib/include"]` 额外包含目录；`defines = ["MY_FEATURE=1"]` 宏；`cflags = []` 和 `ldflags = []` 额外编译/链接参数。路径相对工程根目录。普通 APP 不需要此节。

打包检查 `initial` / `max` 内存大于 0 且按 64 KiB 对齐，不为所有板规定同一个上限。各板能分到多少线性内存由该板固件配置决定，设备启动时仍按当时的连续空闲块分配。可见像素缓冲也消耗 APP 内存。不要只计算业务数组而忽略表面、运行时堆和栈；不能把 PSRAM 硬件总量当作 APP 内存上限。屏幕宽高用 `aster_display_get_info` 读取，不要在 SDK 里按板号假设 240×320 或 320×480。部署使用 `aster deploy PACKAGE --target xtensa --port 串口`。

## 4. 完整可构建示例

下面是带触摸按钮、持久化计数和字体异步重绘的图形 APP。绘图使用普通 RGB565 表面，每次重绘清屏后绘制完整场景；页面尺寸从运行时读取。需要设备显示和触摸能力；计数写入失败时保留会话内计数并输出错误。

### 文件：app.toml

```toml
[app]
id = "com.example.counter"
name = "计数器"
version = "1.0.0"
abi = "3.7.0"
abi_min = "3.7"
aot = true
optimize = "Os"
orientation = "portrait"
category = "tool"
stack = 16384
initial_memory = 393216
max_memory = 1048576
heap = 262144
requires = ["display", "touch"]
permissions = ["kv"]
sources = ["src/main.c"]

[sdk]
target = "xtensa"
```

### 文件：src/main.c

```c
#include <aster_imports.h>
#include <aster_gfx_engine.h>

static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[128];
static uint8_t text_arena[2048];
static uint32_t width, height, count;
static uint32_t button_x, button_y, button_w, button_h;
static int dirty, pressed;

static void release_resources(void)
{
    if (surface != ASTER_HANDLE_INVALID) {
        aster_surface_destroy(surface);
        surface = ASTER_HANDLE_INVALID;
    }
}

static int in_button(uint32_t x, uint32_t y)
{
    return x >= button_x && x < button_x + button_w &&
           y >= button_y && y < button_y + button_h;
}

static int32_t draw_text(int32_t x, int32_t top, uint32_t px,
                         uint16_t color, const char *text)
{
    return aster_ge_text(&ge, x, top + aster_text_asc((int)px), px,
                         color, ASTER_GFX_NOBG, text);
}

static int32_t render(void)
{
    char label[48];
    int32_t rc;
    uint16_t bg = ASTER_RGB565(18, 24, 34);
    uint16_t muted = ASTER_RGB565(166, 180, 198);
    uint16_t accent = pressed ? ASTER_RGB565(24, 91, 165)
                              : ASTER_RGB565(36, 120, 216);

    aster_ge_clear(&ge, bg);
    rc = draw_text(16, 20, 20, ASTER_WHITE, "计数器");
    if (rc < 0) return rc;
    aster_snprintf(label, sizeof(label), "计数 %u", count);
    rc = draw_text(16, 70, 20, ASTER_WHITE, label);
    if (rc < 0) return rc;
    aster_ge_rect_r(&ge, (int32_t)button_x, (int32_t)button_y,
                    button_w, button_h, accent, 10, 0);
    rc = draw_text((int32_t)button_x + 16, (int32_t)button_y + 14,
                   20, ASTER_WHITE, "点击 +1");
    if (rc < 0) return rc;
    rc = draw_text(16, (int32_t)height - 32, 16, muted, "计数自动保存");
    if (rc < 0) return rc;
    return aster_ge_present(&ge); /* >= 0: commands executed */
}

ASTER_APP_EXPORT int aster_app_init(void)
{
    aster_display_info_t info = { .size = sizeof(info), .version = 1 };
    int32_t rc = aster_display_get_info(0, &info, sizeof(info));
    if (rc < 0) return rc;
    width = info.app_width;
    height = info.app_height;
    if (width < 160 || height < 220) return ASTER_ENOTSUP;

    aster_surface_desc_t desc = {
        .size = sizeof(desc), .version = 1,
        .width = width, .height = height,
        .format = ASTER_PIXEL_RGB565, .flags = 0
    };
    rc = aster_surface_create(&desc, sizeof(desc), &surface);
    if (rc < 0) return rc;
    aster_ge_init(&ge, surface, width, height, commands,
                  sizeof(commands) / sizeof(commands[0]),
                  text_arena, sizeof(text_arena));
    button_x = 16;
    button_w = width - 32;
    button_h = 54;
    button_y = height / 2;
    count = 0;
    uint32_t saved = 0;
    rc = aster_kv_get("count", 5, &saved, sizeof(saved));
    if (rc == (int32_t)sizeof(saved)) count = saved;
    else if (rc < 0 && rc != ASTER_ENOENT)
        aster_logf(ASTER_LOG_WARN, "load count failed: %d", rc);
    pressed = 0;
    dirty = 1;
    rc = aster_app_set_framerate(30);
    if (rc < 0) { release_resources(); return rc; }
    aster_printf("counter init %ux%u", width, height);
    return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
    if (!ev || surface == ASTER_HANDLE_INVALID) return;
    if (ev->type == ASTER_EVENT_FRAME) {
        int fonts_changed = aster_gfx_fonts_pending();
        if (dirty || fonts_changed) {
            int32_t rc = render();
            if (rc < 0) {
                aster_errfmt("render failed: %d", rc);
                aster_app_exit(rc);
                return;
            }
            dirty = 0;
        }
    } else if (ev->type == ASTER_EVENT_TOUCH && ev->u.touch.slot == 0) {
        uint32_t action = ev->u.touch.action;
        if (action == ASTER_TOUCH_DOWN) {
            pressed = in_button(ev->u.touch.x, ev->u.touch.y);
            dirty = 1;
        } else if (action == ASTER_TOUCH_UP) {
            if (pressed && in_button(ev->u.touch.x, ev->u.touch.y)) {
                ++count;
                int32_t rc = aster_kv_set("count", 5, &count, sizeof(count));
                if (rc < 0) aster_errfmt("save count failed: %d", rc);
            }
            pressed = 0;
            dirty = 1;
        }
    } else if (ev->type == ASTER_EVENT_KEY && ev->u.key.pressed &&
               ev->u.key.code == 1) { /* evdev ESC */
        aster_app_exit(0);
        return;
    }
}

ASTER_APP_EXPORT void aster_app_resume(void) { pressed = 0; dirty = 1; }
ASTER_APP_EXPORT void aster_app_stop(void) { release_resources(); }
ASTER_APP_EXPORT void aster_app_destroy(void) { release_resources(); }
```

`stop` 和 `destroy` 都调用幂等清理，因此无重复释放。初始化只准备资源，第一帧真正绘制；不在初始化里等待输入或运行动画。示例使用 `kv`，完全不需要网络。

## 5. 生命周期、事件和时间

APP 导出函数的准确签名如下（由 APP 实现，不是调用固件的 import）：

```c
int aster_app_init(void);
void aster_app_event(const aster_event_t *ev);
void aster_app_start(void);
void aster_app_pause(void);
void aster_app_resume(void);
void aster_app_stop(void);
void aster_app_destroy(void);
```

正常启动：`init → start → event(多次)`。`init` 返回 `ASTER_OK` 才接受启动；错误返回负值。暂停/恢复对应 `pause/resume`；结束时可调用 `stop/destroy`。可选生命周期钩子不实现也有 SDK 默认函数；必须自行实现 init 和 event，因为默认 event 会直接请求退出。

`aster_app_exit(code)` 请求结束当前 APP，调用后立即从回调返回，不继续绘图或启动其他异步操作。回调顺序由运行时控制，不要自行调用 start/stop 来模拟退出。事件指针只在本次回调使用，需要保留的数据复制到自己的状态。

事件 payload：

- FRAME：`u.frame.frame` 帧序号；每帧推进业务/重绘，默认 30 FPS。
- TOUCH：`u.touch.x/y` 是 APP 坐标，`action` 为 DOWN/MOVE/UP，`slot` 为触点编号。按钮建议 DOWN 记录命中，UP 在范围内才触发；滑动记录起点和移动差值。
- TIMER：`u.timer.id` 等于创建的 timer handle，`u.timer.user` 是传入的自定义数值。
- KEY：`u.key.code` 是 evdev 原始编号，`pressed` 为 1/0。ESC=1、退格=14、Enter=28、空格=57、上=103、左=105、右=106、下=108。不要假设设备都有键盘。
- SYS：`u.sys.code` 为 MEM_PRESSURE 或 ORIENT，`value` 携带系统值。内存压力时可释放缓存；方向变化时重新查询显示信息，必要时销毁并重建表面、布局。

事件版本当前为 1，结构完整定义见第 12 节；没有 ASTER_EVENT_UI 或固件原生控件 API。APP 自行绘制按钮、列表、输入框和命中判断；触摸硬件不自动提供输入法或手势识别。

`aster_app_set_framerate(fps)`：0 使用默认 30，大于 60 被限为 60。慢任务不能靠提高 FPS 解决；每次回调应尽快返回。相机采集、录音、文件解码是明确会阻塞的接口，安排在用户触发时调用并提供状态反馈，不逐帧调用。

定时器示例（在 init 创建，在 event 处理，在 stop/destroy 销毁）：

```c
static aster_handle_t tick;
/* init 内：repeat=1 重复，0 单次；user=42 用于区分用途。 */
int32_t rc = aster_timer_create(1000, 1, 42, &tick);
/* event 内： */
if (ev->type == ASTER_EVENT_TIMER && ev->u.timer.id == tick) {
    /* 更新业务状态，设置 dirty，随后返回。 */
}
/* 幂等清理： */
if (tick) { aster_timer_destroy(tick); tick = 0; }
```

轮询触摸或按键时，参数是 **u32 偏移**，需要显式转换，不是句柄：

```c
aster_touch_point_t points[2];
int32_t n = aster_input_touch((uint32_t)(uintptr_t)points, 2);
uint32_t keys[8];
int32_t held = aster_input_keys_state((uint32_t)(uintptr_t)keys, 8);
```

优先使用 TOUCH/KEY 事件避免重复触发。轮询结果返回写入数量或负错误。

两种时间：

- `aster_time_now_ms()` 返回 **uint32_t**，当前 APP 启动后经过的单调毫秒数，约 49.7 天回绕。间隔计算用 `(uint32_t)(now - previous)`，不要当 UTC。
- `aster_time_get(&info, sizeof(info))` 返回 UTC `unix_ms` 与设备本地年月日时分秒，ABI 3.6。`flags & ASTER_TIME_FLAG_SYNCED` 表示已同步；未同步也可能返回默认时间。调用本身不触发网络。

```c
aster_time_info_t now = { .size = sizeof(now), .version = 1 };
if (aster_time_get(&now, sizeof(now)) == ASTER_OK) {
    aster_printf("local %d-%u-%u %u:%u:%u", now.year, now.month,
                  now.day, now.hour, now.minute, now.second);
}
```

## 6. 绘图、字体和布局

### 推荐：命令绘图

`aster_surface_create` 创建 RGB565 表面，`aster_ge_init` 初始化命令队列；每帧用 ge 绘制，`aster_ge_present` 一次提交。创建时宽高填显示返回的 `app_width/app_height`，0 则让运行时选默认大小。不要把面板的 `width/height` 当作 APP 坐标。

`aster_ge_init` **需要 8 个参数**：上下文、surface、宽、高、命令数组、命令容量、文本 arena、arena 字节容量。请直接使用第 4 节准确写法。命令是 24 字节；128 条命令需要 3072 字节。建议静态数组，按单次完整重绘的命令数量和 UTF-8 文本总字节量预留容量。队列满会自动提交；不要依赖中途提交实现完整页面，尤其 panel-canvas 模式应让整帧装入队列。

所有颜色均 RGB565。`ASTER_RGB565(r,g,b)` 输入三通道 0–255；有 BLACK/WHITE/RED/GREEN/BLUE/YELLOW/CYAN/MAGENTA 常量。ge 图形参数使用像素，命令编码坐标为有符号 16 位、宽高为无符号 16 位，避免越界值。

ge_present/ge_exec/aster_gfx_exec 成功返回非负的命令执行数，**不能用 rc != 0 判断失败**。ge_text 返回负值表示文本 arena 容量不足等错误。绘图提交后命令数组和文本 arena 会重置；下一帧重新编码。

**纯命令绘制的脏区提交：**如果这一帧仅使用 `aster_ge_rect/text/bmp` 等命令，
可用 `aster_ge_present_commands(&ge)` 代替 `aster_ge_present(&ge)`。固件按实际栅格
结果收集有界区域，避免将两个远离的小更新合成大矩形；返回值仍检查 `< 0`。
不要在这种模式混用直接写 Guest 像素或手工 `aster_ge_touch`；手绘/命令混合页面
继续使用原 `aster_ge_present` 和显式更新区域。旧 Host 可执行这种隐式区域调用，
但是否保留多区域取决于 Host 实现，不能保证相同提速。旧自动分批仍会分别发布，
需要一次逻辑发布时使用下面的帧事务。

普通表面事务会保留 BEGIN 时的 Guest 像素；成功 END 后只写回实际绘制区域。
首帧、其他界面覆盖屏幕、方向改变或 END 失败后完整恢复，之后可以按脏区提交；
无变化的保留帧可以不发送像素。Canvas 仍要求全帧 CLEAR/重绘，scale-fit 仍走
完整适配路径。微雪通常仍发送整屏像素，减少绘制/搬运不等于减少 QSPI 字节。

**可选完整帧事务（需查询 Host 支持，未发布的本地协议 v1）：**

```c
int32_t rc = aster_ge_begin_frame(&ge);
if (rc < 0) return rc;  /* ENOTSUP: 旧 Host，不能假定帧原子性 */
aster_ge_clear(&ge, 0x0000);
/* 可以多次编码并自动溢出；这些批次只写私有 PSRAM 帧。 */
aster_ge_text(&ge, 8, 24, 16, 0xffff, ASTER_GFX_NOBG, "Hello");
rc = aster_ge_present(&ge);  /* 成功为 0，一次整帧发布并等待传输完成 */
if (rc < 0) return rc;      /* 不按已发布帧推进动画状态 */
```

`aster_ge_begin_frame()` 要在空命令环且无未处理错误时调用。`aster_ge_present()` 会结束事务，编码或批次失败则取消；也可显式调用 `aster_ge_cancel_frame()`。不调用 begin 的旧 APP 保留即时提交语义。旧 Host 通过现有导入探测并返回 ENOTSUP；因此 APP 仍能加载，但必须自行决定是否接受旧路径可能发布半帧的行为，SDK 不自动降级。

事务中只使用 ge/图形命令；不要向 acquire 得到的 Guest 像素指针直接写入，也不要调用 surface_present/flush。普通 Guest 缓冲在成功结束后才更新，失败绘制/取消时不变。Panel canvas 的首批首条必须 CLEAR，首版完整重绘。每个 APP 至多一个活动事务，最多 153600 像素、256 批、8192 条命令、128 KiB 文本 arena 累计、1000 ms 绘制期限；暂存缓存容量最多 300 KiB PSRAM（扩容先分配再释放，短暂峰值最多 600 KiB），会缓存复用，APP 退出或对应表面销毁时释放。帧结束等待现有显示任务完成，首版没有流水线重叠收益。

暂停或销毁会撤销事务，帧 ID 防止撤销后的旧批次按即时路径上屏。失败绘制保持上一张发布画面；LCD 物理传输在 END 期间发生故障时可能已有部分像素到达屏幕，返回错误不能保证光学回滚，也没有声称具备硬件垂直同步页翻转。此协议复用既有导入但新增可协商语义，ABI 3.7 的版本号本身不保证 Host 已实现，需要实际探测；正式发布组合仍待后续更新。


### 普通表面和 panel-canvas

普通表面设置 `.flags = 0`。需要直接操作像素时：

```c
aster_surface_buffer_t b = { .size = sizeof(b), .version = 1 };
int32_t rc = aster_surface_acquire(surface, &b, sizeof(b));
if (rc == ASTER_OK && b.ptr && b.format == ASTER_PIXEL_RGB565) {
    aster_gfx_t pixels;
    aster_gfx_init(&pixels, b.ptr, b.width, b.height, b.pitch);
    aster_gfx_clear(&pixels, ASTER_BLACK);
    aster_surface_present(surface);
}
```

`b.pitch` 是**字节/行**，不要当作像素。每次绘制重新 acquire；指针只在当前 present 前使用。混合像素写入与 ge 命令时，用 `aster_ge_touch` 标记手写像素的脏区域，或明确提交完整矩形。

`ASTER_SURFACE_F_PANEL_CANVAS` 可请求固件原生缓冲来减少 APP 内存和像素转换。只是请求，可能退回普通表面。acquire 返回 flags 中有该标志时，**ptr 是 NULL**，只能通过 gfx 命令画；不能使用普通像素辅助函数。

panel-canvas 的双缓冲轮换不保留上帧背景，**每次 present 必须先全屏 clear，再完整绘制页面所有元素**，让提交覆盖整个画布。只画变化的文字/棋子会出现残留、黑屏或闪烁。可以按需少画帧，但画出的每一帧必须完整。需要读取像素、外部解码写入或局部更新时使用普通表面。

`ASTER_SURFACE_F_SCALE_FIT`（`0x00000002`）是可选的兼容画布，应用自己指定宽高，这块缓冲就是应用坐标。present 时固件按 Godot keep 把它铺到玻璃上：`scale = min(面板宽/缓冲宽, 面板高/缓冲高)`，可以放大也可以缩小，结果居中，多出来的边涂黑。属于该表面的触摸会减去边距、除以同一比例，再夹进缓冲范围。不要和 `ASTER_SURFACE_F_PANEL_CANVAS` 同时设置。不设这个标志的表面仍与屏幕 1:1。`apps/game.2048` 和 `apps/snake` 不使用它。

`aster_surface_flush(surface, src, x,y,w,h,src_pitch)` 可从独立 RGB565 数组推送矩形。这里 `src_pitch` 是**像素/行**，与 acquire 的字节 pitch 不同；区域必须完全在表面范围内。flush 源不一定是 acquire 缓冲，不要假设它更新了后续 ge 绘制依赖的整个持久缓冲。

### 字体和中文

APP 的名称、简介和图标由安装包提供。流光桌面的固定文字与动态 APP 文字统一使用固件提供的通用 SD 字库和原生字号缓存。新增、改名或升级 APP 不要求添加固件内置字形，也不要求重新编译固件。桌面光带缩放与立创按钮尺寸属于固件布局，不改变 APP 的字体 API 或业务坐标。

固件提供 12、14、16、18、20、24、28、32、40、48px 共 10 档实际字号，每档 SD 字库覆盖源字体的全部约 2.9 万个字符。字体位图按该字号烘焙，绘制时不缩放字形；其他请求字号选择最近的一档。行高与基线以 SDK 契约里的 `font_metrics.h` 为准。

用 `aster_text_for_display(reference_px, width, height)` 按当前画布分辨率选择实际字号，`reference_px` 是 320×480 下的文字角色大小。再用 `aster_text_line_h(px)`、`aster_text_asc(px)` 布局；`ge_text` 的 y 是基线，首行基线为顶边加 asc。

```c
int body_px = aster_text_for_display(16, canvas_width, canvas_height);
int title_px = aster_text_for_display(20, canvas_width, canvas_height);
int baseline = top + aster_text_asc(body_px);
```

桌面与 APP 共用完整 SD 字库加载器。每个实际使用字号按需申请独立 PSRAM LRU 缓存：位图空间约 8–48KiB，上限 192 个字形，另有索引和 A8 临时空间。缓存满时逐个淘汰最久未使用字形。完整字库始终在 SD；不是只支持缓存里的字符。APP 初始化前释放桌面缓存并准备适合当前屏幕的常用字号，退出或初始化失败后释放 APP 缓存；返回桌面重新预热。冷字形会同步读 SD，常用字符在启动阶段预热；尚未访问的字符首次出现仍可能产生 I/O。

字号资源由固件资源发布维护，APP 不携带字体、不修改固件字形子集。更新字号契约后需同时更新设备 SD 字库；SDK 本地度量表必须匹配设备发布版本。`aster sys fonts` 可以查看各字号覆盖字符数、缓存字形数和实际申请字节数。

UTF-8 长度参数是**字节数**，使用 `strlen`；不是汉字个数。文字宽度使用 `aster_gfx_text_w(px, text, byte_len)`，不要用字符数乘字号。文字渲染受已安装字体字符覆盖范围限制；不要假设 emoji、所有 Unicode 符号均可显示，省略号用三个 ASCII 点 `...`。

字体从 SD 异步加载，初次绘制可暂时留空。按事件重绘的 APP 在**每个 FRAME** 调用 `aster_gfx_fonts_pending()`，返回 1 就重新绘制；该函数非阻塞且不返回错误。像第 4 节先单独调用，再合并 dirty，避免短路求值跳过推进加载。持续渲染的游戏自然会重画。

中文绘制用 ge_text。`aster_gfx_text` 是 APP 像素侧 5×7 ASCII 小字，不是 UTF-8 中文渲染。

布局建议：20 px 标题、16 px 次要信息；页面边距 12–16 px，按钮高 44–54 px；列表行高根据 line_h 加 6–10 px 间距，扫描/文件列表采用滚动视窗，避免为每条项目创建大像素缓存。文字超宽用 `aster_text_elide` 截断；多行用 `aster_text_line`，见辅助库参考。

## 7. 文件、资源与持久化

### 文件路径与资源

声明 `permissions = ["fs"]`；额外操作 SD 根还需 `"sd"`。APP 使用以下沙箱路径，不使用硬件挂载点：

- `/assets/...`：打包资源，安装在该 APP 的 assets 目录。按资源使用，不在这里保存配置。
- `/data/...`：本 APP 的持久数据。
- `/cache/...`：本 APP 的缓存。
- `/music/...`：设备共享音乐库，按只读路径使用。
- `/sd/...`：SD 根路径，仅显式有 fs 和 sd 权限的 APP 使用。

使用绝对沙箱路径，不使用 `..`、Windows 盘符或 `/SD:` 作为 APP 内路径；`/SD:` 是设备部署工具使用的挂载路径。

例如工程内文件 `audio/click.wav`：

```toml
# 以下字段放在 [app] 中，不要放到 [sdk] 或 [build] 下。
assets = ["audio/click.wav", "images/background.png"]
permissions = ["fs"]
```

运行时对应 `/assets/audio/click.wav` 和 `/assets/images/background.png`。`assets` 只会打包显式列出的文件；目录存在不代表文件已入包。资源必须位于工程目录内，不允许逃逸路径。

基本文件读取：

```c
aster_handle_t file = 0;
const char *path = "/assets/config.txt";
int32_t rc = aster_fs_open(path, (uint32_t)strlen(path),
                          ASTER_FS_O_RDONLY, &file);
if (rc == ASTER_OK) {
    char chunk[128];
    int32_t got = aster_fs_read(file, chunk, sizeof(chunk) - 1);
    if (got >= 0) chunk[got] = '\0';
    aster_fs_close(file);
}
```

read/write 返回字节数或负错误，read=0 是 EOF。读取/写入可能是部分长度，需要循环处理，并让大任务分批跨事件执行。打开失败不能继续使用句柄。目录用 opendir、dir_next、closedir；dir_next 返回名字字节数，0 结束，负数错误。

`aster_fs_seek` 返回状态（成功 0），不是 POSIX `lseek` 的新文件偏移。文件大小用 stat 的 `filesize`（uint64_t）。创建 `/data/subdir` 可用 mkdir，EEXIST 可视作目录已存在；每个打开的文件/目录都在退出路径关闭。

### KV 配置

声明 `permissions = ["kv"]`。KV 按 APP ID 隔离，适合小型设置、高分和进度。key 使用短 ASCII 名，当前固件 KV key 上限为 48 字节，不要超过此长度。set 写入任意字节 blob；get 返回长度或负错误。为固定结构准备足够缓冲，只有长度等于预期时才解读；复杂结构增加版本字段。不存在时使用默认值。不要每 FRAME 写入，应在用户确认、关卡结束等状态变更点保存。

### 图标

`icon = "icon.bin"` 要求 **ASTI**。可省略图标，核心 APP 构建不依赖图标转换。若已有 ASTI 文件直接引用；彩色图标推荐 RGB565A8，48×48，最长边不要超过 launcher 的 128 像素接收上限。

Linux 源码 SDK 支持以下转换命令，PNG 需额外 Pillow，SVG 还需 CairoSVG 与其系统依赖；SDK 普通 install 不负责这些可选依赖：

```bash
# 已激活 SDK 环境；只有需要生成图标时执行。
python -m pip install pillow
python "$ASTER_SDK/tools/icon_convert.py" icon.png -o icon.bin --format rgb565a8 --size 48
```

格式选项 `i1`、`a8`、`rgb565`、`rgb565a8`。Windows 三 EXE 的统一 CLI 没有 icon-convert 命令；可使用已生成的 ASTI，或在额外安装 Python + Pillow 后执行同一个转换脚本。执行脚本无需阅读其源码。

如需完全不依赖转换工具生成纯色图标，任何主机语言可按下面规则写二进制：16 字节小端头部：bytes 0–3 为 `ASTI`，4–5 为 uint16 版本 1，6–7 为宽，8–9 为高，10 为格式 3（RGB565），11 为 0，12–15 为 uint32 payload 长度 `w*h*2`，随后按行写小端 RGB565 像素。APP 只声明该文件，launcher 负责展示。

## 8. 音频、图片、传感器与相机

### 音频

需要声音的 APP 声明 `requires = ["audio-out"]`，可选声音则声明 optional 并用 `aster_system_has_feature(ASTER_FEATURE_AUDIO_OUTPUT)` 检测。加载文件还需 fs 权限。

先 `aster_audio_get_info` 查询当前设备的 `sample_rate/channels/bits`；不能硬编码音频格式。原生 PCM 为小端有符号 16 位交错采样的参考实现，实际参数以查询为准。

背景音乐：

```c
const char *music = "/assets/audio/music.wav";
int32_t rc = aster_audio_start_file(music, (uint32_t)strlen(music));
```

start_file 后台流式播放 WAV/PCM/MP3，**替换当前背景会话**，不是每次调用叠加声音。用 pause/resume/stop 控制；get_state 读取播放状态、进度和时长，version=2。没有通用音频完成事件，按低频 timer/FRAME 读取状态即可。未知时长 `msec_total` 可能为 0。

短音效（ABI 3.4）：effect_load 返回**正句柄**或负错误；上限四个，每个最多 64 KiB 的原生格式立体声 PCM WAV。先加载音效再启动背景音乐；effect_play 与音乐混音，不替换音乐；重放同句柄重启该音效。退出 effect_unload，不要每次点击重新解码加载。

```c
int32_t cue = aster_audio_effect_load("/assets/audio/click.wav", 23);
if (cue > 0) aster_audio_effect_play((uint32_t)cue);
/* stop/destroy 中，对仍持有的正句柄： */
if (cue > 0) aster_audio_effect_unload((uint32_t)cue);
```

上例路径长度 23，也可以统一用 strlen 防止改文件名后漏改长度。volume 范围 0–100；get_volume 返回该数值或负错误。seek 参数毫秒，MP3 为近似定位。get_lyrics 返回 UTF-8 字节数，不保证 NUL，准备 `capacity-1` 并手动终止。

record 需要 AUDIO_INPUT，填满指定 PCM 缓冲前会阻塞；根据 get_info 计算时长，避免把数秒大录音放在小栈。没有公开流式录音 start/read/stop 接口。旧 aster_audio_play/start 已删除，不能使用。

### 图片

小精灵：`aster_gfx_bmp_load(path,len,cap_w,cap_h,&handle,&w,&h)` 将 BMP/PNG/JPEG 缩小到尺寸限制内缓存于固件，需 fs。成功状态 0，句柄从 out 读取；在 ge_bmp 中传返回的 w/h。scale=256 表示 1:1，alpha=255 表示完全显示；黑色 RGB565 `0x0000` 是该精灵绘制接口的透明色键。不要把它当作保留任意 PNG alpha 的通用 RGBA 合成器。结束用 bmp_free。

照片预览：先 img_probe 判断格式、原图尺寸、输出估计与 flags/why；再 img_render 解码到 APP 自备 RGB565 连续数组。cap_w/cap_h 限定输出，保持比例且不放大；dst_span 是可用**字节数**；dst_off 用 `(uint32_t)(uintptr_t)pixels`。结果行距为 out_w 个像素，然后 surface_flush：

```c
/* pixels 的容量至少 cap_w*cap_h*2，使用普通表面。 */
aster_img_render_args_t args = {
    .size = sizeof(args), .version = 1,
    .dst_off = (uint32_t)(uintptr_t)pixels,
    .dst_span = sizeof(pixels),
    .cap_w = cap_w, .cap_h = cap_h
};
int32_t rc = aster_img_render(path, (uint32_t)strlen(path), &args, sizeof(args));
if (rc == ASTER_OK)
    aster_surface_flush(surface, pixels, 0, 0, args.out_w, args.out_h, args.out_w);
```

该片段的 pixels/path/cap_w/cap_h 由业务定义，不是单独可编译文件。probe/render 可能阻塞，不能每帧重新解码。不支持的图片变体以错误或 flags 表达，检查后展示提示。

### 传感器

检测 ACCELEROMETER/GYROSCOPE 后，用 `float xyz[3]` 和 `sizeof(xyz)` 调用 accel_read/gyro_read。加速度单位 m/s²（包含重力），陀螺仪 rad/s；拉取模式，没有专用传感器事件。按合适帧率采样并处理 ENODEV/EIO。

### 六轴 IMU（ABI 3.7，推荐）

APP 需要 `requires = ["accelerometer", "gyroscope"]`，不需要权限、打开句柄或设备驱动。头文件已由 `<aster_imports.h>` 引入。

`aster_imu_get_info(&info, sizeof(info))` 检查六轴传感器可读性，返回有效通道 flags 和 `min_interval_ms`。当前 10 ms 是原生采样缓存 TTL，不是硬件 ODR，也不保证每 10 ms 自动推送事件。

`aster_imu_read(&sample, sizeof(sample))` 同时读出加速度和角速度：同一次原生 fetch、同一时间戳/序号，避免分别读取六轴造成时间错位。加速度 `accel[3]` 单位 m/s²，包含重力；角速度 `gyro[3]` 单位 rad/s。轴属于传感器坐标，不随 APP 屏幕旋转改写。需要显示方向映射时由 APP 自行处理。

```c
aster_imu_info_t info = { .size=sizeof(info), .version=1 };
int32_t rc = aster_imu_get_info(&info, sizeof(info));
aster_imu_sample_t sample = { .size=sizeof(sample), .version=1 };
if (rc == ASTER_OK) rc = aster_imu_read(&sample, sizeof(sample));
if (rc == ASTER_OK) {
    /* 检查 sequence 再积分。timestamp_ms 是设备单调 uptime，不是 UTC。 */
}
```

两个接口 V1 都要求完整结构大小；成功才写输出。EINVAL 为参数/线性内存范围错误，ENODEV 为六轴能力不存在或设备未就绪，EIO 为采样或通道读取失败。调用属于短同步传感器读取，不是后台订阅；在 FRAME/timer 中采样，避免紧循环轮询。缓存窗口内可返回相同 timestamp/sequence，姿态积分必须每序号只处理一次。sequence 为 u32 会回绕；判断是否变化，不使用大小关系判断更新。

### APP 侧姿态辅助库

包含 `<aster_imu_filter.h>`，使用 SDK 的六轴互补滤波，不依赖 `math.h` 或固件私有 API。`aster_imu_pose_t` 的 `q[4]` 为 w/x/y/z 四元数，表示传感器坐标转到世界坐标；`bias[3]` 为 APP 设置的静态陀螺零偏（rad/s）。首次有效样本按重力初始化倾斜，之后用角速度积分和重力修正俯仰/横滚。没有磁力计，无法绝对定位航向，yaw 长期可能漂移。强线性加速度期间暂时跳过重力修正。

```c
/* 下列为调用参考；不需要在 APP 中复制声明。 */
typedef struct {
    float q[4];
    float bias[3];
    uint64_t last_ms;
    uint32_t last_sequence;
    uint32_t started;
} aster_imu_pose_t;
void aster_imu_pose_init(aster_imu_pose_t *pose);
int32_t aster_imu_pose_update(aster_imu_pose_t *pose, const aster_imu_sample_t *sample);
void aster_imu_quat_rotate(const float q[4], const float vector[3], float out[3]);
void aster_imu_pose_relative(const aster_imu_pose_t *pose, const float reference[4], float out[4]);
```

pose_update 返回 1=已处理新样本、0=重复样本、负值=非法样本。间隔超过 250 ms 或时间倒退时重新建立积分时基，不把旧角速度积分整个间隙。pause/resume 后可以将 last_ms=0 来跳过第一次间隔。quat_rotate 可在输入/输出同数组时使用。pose_relative 返回 reference 的逆旋转乘当前姿态，用于“归零”显示；先将 pose.q 复制到自己保存的 float reference[4]，不会修改传感器原始数据。不要修改 helper 的时基字段来模拟数据更新。

调用模式（init 里 pose_init，每个 FRAME 内）：

```c
/* pose 为静态 aster_imu_pose_t；sample 为本次输出结构。 */
if (aster_imu_read(&sample, sizeof(sample)) == ASTER_OK) {
    int32_t changed = aster_imu_pose_update(&pose, &sample);
    if (changed > 0) {
        /* 用 pose.q 旋转 3D 顶点、透视投影、ge_tri/ge_line_aa 绘制。 */
    }
}
```

静置校准由 APP 完成：检查加速度模长接近 9.8 m/s²、角速度接近 0，连续采样约 2 秒，平均 gyro 并写入 pose.bias；运动时清空累计，提示继续静置。归零只是改变显示参考，不替代零偏校准。

立方体示例的显示坐标映射为相对四元数 `[w,x,y,z] → [w,-x,-y,z]`，对应绕 Z 轴 180° 的坐标基变换：反转 roll/pitch、保持 yaw。这只影响绘制，不修改原始六轴读数或滤波状态；其他设备的可视化应按实际安装方向选择映射。

SDK 自带 `apps/imucube`：30 FPS 六轴采样、彩色 3D 立方体、重力修正、静置校准、归零按钮（将当前姿态设为参考，让正面朝向屏幕）；普通表面和 panel-canvas 都完整清屏重画。构建：`aster build /absolute/path/to/aster-sdk/apps/imucube --target xtensa`；产物在该 APP 工程的 dist/xtensa/aster.imucube.ast。读取本节即可开发其他 IMU APP，无需先读取示例代码。

### 相机

先 cam_info 获取 w/h，再准备至少 w*h*2 字节缓冲，cam_read 取得 RGB565 单帧。读取会阻塞 APP 回调，参考设备可能需 1–2 秒；按按钮触发，避免每帧调用。没有公开视频预览流 API。相机缓冲过大时先评估 2 MiB 线性内存上限，不要随意增加 max_memory。

## 9. 系统能力与其他接口

`aster_system_has_feature(ASTER_FEATURE_...)` 的参数是**特性索引**，例如 DISPLAY=0、WIFI=7，不是 `1u << index`。返回 1 有、0 没有；错误为负时同样不能使用。`aster_system_get_abi_version()` 返回打包 u32：`0x00MMmmPP`，当前 `0x00030800`。需要额外检查时比较主版本兼容及最低版本，不能强制未来 minor 必须完全相等。

虽然头文件声明了 `aster_system_info_t`，当前公开 API 没有 `aster_system_get_info()`。不要为该结构猜测查询函数。

appmgr 需要 appmgr 权限：count 返回安装数；list 用 `aster_app_info_t` version=2 获取 ID、名称、状态、分类和图标信息。start 是单 APP 的切换，调用方会先 stop/destroy，然后启动目标，不是并行启动后台 APP；拒绝启动自己。uninstall 拒绝删除自己或正在运行/暂停的应用。

ABI 3.8 新增TCP/UDP通信，完整接口和约束见第15节；需要network能力和net权限。DNS由连接/发送中的域名解析提供，不单独暴露解析器。Wi-Fi station接口仍需wifi能力和权限，连接提交不等于取得IP，检查status.ipv4。个人热点APP控制、HTTP/HTTPS、TLS、MQTT、WebSocket尚未提供。

其他不存在的接口：没有公开蓝牙通信、线程创建、异步任务通用框架、桌面式文件选择器、原生输入框/键盘、UI 控件树。需要这些功能时在现有能力内自行实现交互，或提出明确 SDK 扩展需求。

## 10. 构建、部署与排错

### 产物与校验

```text
aster build
aster verify dist/xtensa/com.example.counter.ast --target xtensa
```

生成 `build/xtensa/<app-id>/app.wasm`、有 AOT 时的 `app.aot`、`manifest.json`、编译中间文件，以及 `dist/xtensa/<app-id>.ast`。包用 ASTP 容器，包括 WASM、可选目标 AOT、manifest、ASTR 资源、可选 ASTI 图标；不需要 APP 开发者手写包协议。

构建执行 ABI、目标能力、内存、导入和导出检查，并验证最终包。用 `aster build --clean` 删除项目 build 和 dist 后退出，再运行 build 重建。CLI 也支持 `aster build /absolute/path/to/project` 和 `aster run /absolute/path/to/project`。

可选签名：将 `ASTER_SIGN_KEY` 设为私钥 PEM 路径，再 build；不把私钥打包、提交或写入日志。验签用 `aster verify PACKAGE --target xtensa --pubkey PUBLIC.pem`。安装是否接受未签名包由设备固件策略决定。

### 真机部署

设备应已装与APP最低ABI兼容的 Aster 固件（本版网络APP需3.8）、可用 SD 卡与匹配系统字体。SDK 部署 APP 不等于刷整机固件。

自动端口发现：

```text
aster ports
aster run
```

新固件的文件传输、安装与启动共用 shell 串口。部署目标是平台 `xtensa`，端口由 `--port` 给出，不从板号推断：

```text
aster run --port /dev/ttyUSB0
aster run --no-build --port /dev/ttyUSB0
aster deploy dist/xtensa/com.example.counter.ast --target xtensa --port /dev/ttyUSB0 --run
aster deploy dist/xtensa/com.example.counter.ast --target xtensa --port /dev/ttyACM0 --run
```

Windows 对应 `--port COM24`（替换为实际发现的端口）。便携包按主机平台发布：Windows/Linux 各一个包，包内携带 `xtensa` 平台目标。具体开发板的烧录留在固件仓库。SDK 的 `ASTERPING` 握手包含 `serial=shared` 时，安装、运行自动使用文件传输的同一连接，不需要另指定 `--console`。旧固件不返回该标记时仍使用双串口，可显式指定 `--port /dev/ttyACM0 --console /dev/ttyUSB0`。run = 构建、部署并启动；deploy 只装包，带 --run 才启动。

调试与截图：

```text
aster probe --port /dev/ttyUSB0 --target xtensa
aster shell --console /dev/ttyUSB0 aster sys info
aster debug --console /dev/ttyUSB0 --seconds 10 --read-only
aster screenshot screen.png --console /dev/ttyUSB0
```

上传、安装、截图前关闭独立串口监视器，完成后可在同一端口恢复。一个接收流不能由两个独立程序同时读取。传文件期间固件日志继续输出，SDK 自动识别协议回复；APP 不需要实现传输协议。截图与 shell 也是串口操作，需要设备响应。日志推荐 init 完成、关键操作、失败码，不要每 FRAME 连续输出大量文本。

仅需复制额外文件到设备时：`aster push LOCAL_FILE /SD:/destination --port SERIAL_PORT --target xtensa`。普通 APP 资源优先用 assets 随包交付，不依赖开发机临时目录或手动复制。

### 常见问题

- 找不到 aster：重新激活 SDK 的 env.sh/env.ps1/env.cmd；路径含空格时引用完整路径。
- doctor 缺依赖或编译器：完成 SDK install，使用与当前系统匹配的发布包。不要自行修改工具脚本绕过检查。
- init 失败/ENOMEM：检查实际尺寸、表面和大数组预算、heap/initial/max/stack，不在栈上存整帧，不盲目越过目标 2 MiB 上限。
- capability 或 permission 错误：requires 是硬件需求，permissions 是访问授权；按第 3 节精确拼写。
- 导入不允许：使用本文接口，不调用标准库里未承诺的功能；不能靠 --allow-undefined 将不存在的服务变成可用服务。
- 只有 init 日志无首帧：检查 init 是否阻塞、是否返回错误，检查音乐/录音/相机调用；把耗时操作移出初始化，逐项记录前后日志。
- 页面黑屏/闪烁：检查 panel-canvas 是否全屏清除和完整重绘、是否把 NULL 当像素指针、是否中途多次提交一帧、是否尺寸/pitch 单位写错。
- 中文先空白后不出现：每个 FRAME 调用 fonts_pending 并重绘；确认设备安装了匹配系统字体，避免不覆盖的字符。
- 文字错位/挤在一起：ge_text 使用基线，按实际烘焙字号与 line_h 布局，UTF-8 按字节传长度。
- 图标打包失败：需要 ASTI magic，不是 PNG 改后缀；资源 assets 用相对路径且确实存在。
- AOT 太大或设备加载失败：先减少库和代码规模，WASM 路径仅用于模拟器或主机测试；真机必须修复 AOT 的构建与加载问题。构建成功不代表真机资源一定足够。
- ABI 不匹配：SDK、包和设备版本要匹配，不修改固定契约、target metadata 或 header 来伪造兼容。

交付工程时包含 app.toml、src 和资源原件，说明构建命令、产物路径与实际验证范围。没有真机验证就明确写“构建/包校验通过，真机待验证”。

## 11. SDK C 辅助库参考

这些 helper 在 SDK 库里已提供。以下是调用签名，不需要在工程中重新声明或实现。`aster_ge_t` 和 `aster_gfx_t` 的完整定义由对应 include 提供，APP 可直接声明其对象；应用业务不应访问 ge 内部队列字段。

### 精简 libc 与日志

包含 `<aster_imports.h>` 即可使用：

```c
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);
void aster_logf(int level, const char *fmt, ...);
int aster_snprintf(char *buf, int max, const char *fmt, ...);
/* aster_printf(fmt, ...) = INFO 日志；aster_errfmt(fmt, ...) = ERROR 日志。 */
```

格式化支持 `%d %i %u %x %X %s %c %%` 和 long/unsigned-long 修饰；不要用 `%f`（会输出错误格式标记）。用整数定点转换浮点显示。wasm32 的 long 是 32 位，不能用 `%lu` 或 `%llu` 打印 uint64_t；日志中不要直接把 unix_ms/filesize 传给这些格式。strncpy 不保证截断后的 NUL；snprintf 容量包含终止符。可使用 freestanding `<stdint.h>`、`<stddef.h>`、`<stdbool.h>`、`<stdarg.h>`、`<limits.h>` 的类型/宏。未在上述支持清单中的 libc/libm 函数不作为可移植 APP 契约；动态业务数据优先用有界静态数组。

### 命令引擎与像素辅助函数

`<aster_gfx_engine.h>` 包含 ge、像素辅助函数、UTF-8 文字辅助函数。下面列出可直接调用的 helper 签名：

#### aster_gfx_engine.h

```c
void aster_ge_init(aster_ge_t *g, aster_handle_t surf, uint32_t w, uint32_t h, aster_gfx_cmd_t *ring, uint32_t ring_n, uint8_t *arena, uint32_t arena_cap);
int32_t aster_ge_touch(aster_ge_t *g, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int32_t aster_ge_exec(aster_ge_t *g, uint32_t px, uint32_t py, uint32_t pw, uint32_t ph);
void aster_ge_clear(aster_ge_t *g, uint16_t color);
void aster_ge_rect(aster_ge_t *g, int32_t x, int32_t y, uint32_t w, uint32_t h, uint16_t color);
void aster_ge_rect_r(aster_ge_t *g, int32_t x, int32_t y, uint32_t w, uint32_t h, uint16_t color, uint16_t r, uint16_t outline);
void aster_ge_line_aa(aster_ge_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
void aster_ge_ellipse(aster_ge_t *g, int32_t cx, int32_t cy, uint32_t rx, uint32_t ry, uint16_t color, uint16_t stroke);
void aster_ge_tri(aster_ge_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color);
void aster_ge_grad(aster_ge_t *g, int32_t x, int32_t y, uint32_t w, uint32_t h, uint16_t c0, uint16_t c1, uint16_t horizontal);
int32_t aster_ge_text(aster_ge_t *g, int32_t x, int32_t baseline_y, uint32_t px, uint16_t fg, uint16_t bg, const char *utf8);
void aster_ge_bmp(aster_ge_t *g, uint32_t handle, uint32_t sw, uint32_t sh, int32_t x, int32_t y, uint16_t scale256, uint16_t alpha);
int32_t aster_ge_present(aster_ge_t *g);
int32_t aster_ge_begin_frame(aster_ge_t *g);
int32_t aster_ge_cancel_frame(aster_ge_t *g);
uint32_t aster_ge_default_ring(aster_gfx_cmd_t **ring, uint8_t **arena);
```

#### aster_gfx.h

```c
void aster_gfx_init(aster_gfx_t *g, void *buf, uint32_t w, uint32_t h, uint32_t pitch);
void aster_gfx_pixel(aster_gfx_t *g, uint32_t x, uint32_t y, uint16_t c);
void aster_gfx_clear(aster_gfx_t *g, uint16_t c);
void aster_gfx_fill_rect(aster_gfx_t *g, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
void aster_gfx_frame_rect(aster_gfx_t *g, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
void aster_gfx_hline(aster_gfx_t *g, uint32_t x0, uint32_t x1, uint32_t y, uint16_t c);
void aster_gfx_vline(aster_gfx_t *g, uint32_t x, uint32_t y0, uint32_t y1, uint16_t c);
void aster_gfx_line(aster_gfx_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c);
void aster_gfx_disk(aster_gfx_t *g, int32_t cx, int32_t cy, int32_t r, uint16_t c);
void aster_gfx_char(aster_gfx_t *g, uint32_t x, uint32_t y, char ch, uint16_t fg, uint16_t bg, int invert_bg);
uint32_t aster_gfx_text(aster_gfx_t *g, uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg, int bg_fill);
```

#### aster_text.h

```c
int aster_text_snap(int px);
const aster_text_face_t *aster_text_face(int px);
int32_t aster_text_line_h(int px);
int32_t aster_text_asc(int px);
uint32_t aster_utf8_len(const char *s);
uint32_t aster_utf8_cp(const char *s, uint32_t len);
int32_t aster_text_measure(uint32_t px, const char *s, uint32_t len);
int aster_text_ready(uint32_t px);
uint32_t aster_text_line(uint32_t px, const char *src, uint32_t len, int32_t maxw, uint32_t *out_len);
int32_t aster_text_elide(uint32_t px, const char *src, uint32_t len, int32_t maxw, char *dst, uint32_t dst_max);
```

参数语义补充：rect_r 的 radius 为圆角半径，outline=0 填充、1 轮廓；ellipse 的 stroke=0 填充、1 轮廓；grad 的 horizontal=0 竖向、1 横向。text 的 bg 使用 `ASTER_GFX_NOBG` 表示透明背景；bmp 的 scale 使用 8 位小数，256 为 1 倍，有效范围 8–4095，alpha 0–255。

`aster_ge_touch` 添加脏矩形；exec 指定提交区域，present 使用自动累计区域。`aster_ge_default_ring` 提供 SDK 共享静态默认缓存（96 命令、768 字节 arena），多个绘图上下文应各自分配，推荐第 4 节写法。

像素 helper 的参数使用无符号坐标时，先裁剪负坐标；不要把负数直接转 uint32_t。`aster_gfx_text` 返回绘制宽度，字体固定 5×7，步进 6 像素，不是 ge 中文字体。

文字 helper：

- snap 返回最近字号，face 返回对应 px/line_h/asc 的 `aster_text_face_t` 指针。
- utf8_len 返回开头 UTF-8 序列字节数，utf8_cp 解码首个码点；处理外部输入要限制可读长度。
- measure 计算文字 advance 宽度，建议单行输入；ready 查询当前字体是否具备可用度量。
- line 的 out_len 是本行字节数，返回值是应消费的输入字节数（含可跳过的空白/换行），不是行宽。将该行复制到容量足够的 NUL 结尾缓冲再 ge_text，基线按 line_h 前进；确保消费数大于 0 防止无限循环。
- elide 按 maxw 限宽复制到 dst，加 `...`，成功返回输出字节数，失败负值。dst_max 包含 NUL 空间。

## 12. 完整公开 ABI 参考

以下是本版本全部 `aster_*` 固件导入的类型、常量与函数签名，按模块列出。它们由 `<aster_imports.h>` 自动包含；无需复制声明到 APP 中，也不要手写 import 属性。代码块是参考摘录，不是独立头文件。

通用规则：句柄是 u32，0 无效，不可伪造或跨 APP 共享；带 `out` 的函数通常返回状态，句柄从 out 取得。多数状态返回 0 成功、负错误；读取/计数/绘图等接口的非负返回值见前文。字符串长度为 UTF-8 字节数，不含 NUL，常规调用一律传显式长度；不要将 UINT32_MAX 用作普通字符串长度。带结构和 size 的函数传 `sizeof(结构)`，结构初始化 `.size=sizeof(...)`、`.version=1`（audio_state/app_info 使用 version=2）。`void *`/`const char *` 在 WASM 中自然是线性内存指针；只有声明为 u32 offset 的字段/参数才手动转换 `(uint32_t)(uintptr_t)ptr`。

所有输出长度与错误都先检查再使用。API 成功不会自动说明外设存在、操作完成或输出已 NUL 终止；按模块的语义处理。下面保留的 pixel 常量只说明 ABI 编号，不保证当前目标接受所有格式；命令引擎明确要求 RGB565。

### error.h

```c
typedef enum {
	ASTER_OK = 0,
	ASTER_EINVAL = -1,
	ASTER_ENOMEM = -2,
	ASTER_ENOTSUP = -3,
	ASTER_EPERM = -4,
	ASTER_EBUSY = -5,
	ASTER_ENOENT = -6,
	ASTER_EIO = -7,
	ASTER_ETIMEDOUT = -8,
	ASTER_EEXIST = -9,
	ASTER_EBADF = -10,
	ASTER_ENOSPC = -11,
	ASTER_ESTATE = -12,
	ASTER_EABI = -13,
	ASTER_ECORRUPT = -14,
	ASTER_EAGAIN = -15,
	ASTER_ENODEV = -16,
	ASTER_ESIGN = -17,
	ASTER_EUNSIGNED = -18,
} aster_error_t;
```

### types.h

```c
typedef uint32_t aster_handle_t;

#define ASTER_HANDLE_INVALID ((aster_handle_t)0)

#define ASTER_HTYPE_SURFACE 0x02000000u
#define ASTER_HTYPE_TIMER    0x06000000u
#define ASTER_HTYPE_FILE     0x04000000u
#define ASTER_HTYPE_DIR      0x07000000u

typedef struct {
	uint32_t offset;
	uint32_t length;
} aster_string_t;

typedef enum {
	ASTER_PIXEL_MONO1 = 0x0101,
	ASTER_PIXEL_GRAY8 = 0x0801,
	ASTER_PIXEL_RGB565 = 0x1002,
	ASTER_PIXEL_BGR565 = 0x1102,
	ASTER_PIXEL_RGB888 = 0x1803,
	ASTER_PIXEL_BGR888 = 0x1903,
	ASTER_PIXEL_XRGB8888 = 0x2004,
	ASTER_PIXEL_ARGB8888 = 0x2104,
	ASTER_PIXEL_RGBA8888 = 0x2204,
	ASTER_PIXEL_BGRA8888 = 0x2304,
	ASTER_PIXEL_NATIVE = 0xFFFF,
} aster_pixel_format_t;

#define ASTER_FMT_BIT(f) (1ull << ((f) & 0x3F))
```

### log.h

```c
typedef enum {
	ASTER_LOG_DEBUG = 0,
	ASTER_LOG_INFO = 1,
	ASTER_LOG_WARN = 2,
	ASTER_LOG_ERROR = 3,
	ASTER_LOG_FATAL = 4,
} aster_log_level_t;
```

### system.h

```c
#define ASTER_ABI_MAJOR 3
#define ASTER_ABI_MINOR 7
#define ASTER_ABI_PATCH 0

#define ASTER_ABI_VERSION \
	(((uint32_t)ASTER_ABI_MAJOR << 16) | ((uint32_t)ASTER_ABI_MINOR << 8) | (uint32_t)ASTER_ABI_PATCH)

#define ASTER_FEATURE_DISPLAY      0u
#define ASTER_FEATURE_TOUCH        1u
#define ASTER_FEATURE_MOUSE        2u
#define ASTER_FEATURE_KEYBOARD     3u
#define ASTER_FEATURE_CAMERA       4u
#define ASTER_FEATURE_AUDIO_INPUT  5u
#define ASTER_FEATURE_AUDIO_OUTPUT 6u
#define ASTER_FEATURE_WIFI         7u
#define ASTER_FEATURE_BLUETOOTH    8u
#define ASTER_FEATURE_ACCELEROMETER 9u
#define ASTER_FEATURE_GYROSCOPE    10u
#define ASTER_FEATURE_FILESYSTEM   11u
#define ASTER_FEATURE_RTC          12u
#define ASTER_FEATURE_PSRAM        13u
#define ASTER_FEATURE_FRAMEBUFFER  14u

#define ASTER_ARCH_XTENSA 1u
#define ASTER_ARCH_ARM    2u
#define ASTER_ARCH_RISCV  3u
#define ASTER_ARCH_X86    4u

typedef struct {
	uint32_t size;
	uint32_t version;

	uint32_t abi_version;

	char     platform[16];
	char     board[40];
	char     soc[24];

	uint32_t cpu_arch;
	uint32_t cpu_freq_hz;
	uint32_t cpu_count;

	uint32_t ram_internal;
	uint32_t ram_external;
	uint32_t flash_size;

	uint64_t feature_flags;
} aster_system_info_t;

int32_t aster_system_has_feature(uint32_t feature_bit);
uint32_t aster_system_get_abi_version(void);
```

### app.h

```c
void aster_app_exit(int32_t code);

int32_t aster_app_set_framerate(uint32_t fps);

uint32_t aster_time_now_ms(void);

void aster_log(uint32_t level, const char *msg, uint32_t len);
```

### event.h

```c
#define ASTER_EVENT_FRAME     1u
#define ASTER_EVENT_TOUCH     2u
#define ASTER_EVENT_TIMER     3u
#define ASTER_EVENT_KEY       4u
#define ASTER_EVENT_SYS       5u

#define ASTER_TOUCH_DOWN 1u
#define ASTER_TOUCH_MOVE 2u
#define ASTER_TOUCH_UP   3u

#define ASTER_SYS_MEM_PRESSURE 1u
#define ASTER_SYS_ORIENT       2u

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t type;
	uint32_t flags;
	uint64_t timestamp_ms;
	union {
		struct {
			uint32_t x;
			uint32_t y;
			uint32_t action;
			uint32_t slot;
		} touch;
		struct {
			uint32_t id;
			uint32_t user;
		} timer;
		struct {
			uint32_t code;
			uint32_t pressed;
		} key;
		struct {
			uint32_t code;
			uint32_t value;
		} sys;
		struct {
			uint32_t frame;
		} frame;
		uint32_t data[8];
	} u;
} aster_event_t;
```

### display.h

```c
#define ASTER_ORIENT_PORTRAIT  0u
#define ASTER_ORIENT_LANDSCAPE 1u

typedef struct {
	uint32_t size;
	uint32_t version;

	uint32_t width;
	uint32_t height;
	uint32_t dpi_x100;
	uint32_t native_format;
	uint64_t supported_formats;
	uint32_t refresh_rate;
	uint32_t orientation;
	uint32_t flags;

	uint32_t app_width;
	uint32_t app_height;
} aster_display_info_t;

int32_t aster_display_count(void);
int32_t aster_display_get_info(uint32_t index,
							   aster_display_info_t *info, uint32_t info_size);
```

### surface.h

```c
#define ASTER_SURFACE_F_PANEL_CANVAS 0x00000001u
#define ASTER_SURFACE_F_SCALE_FIT 0x00000002u

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t width;
	uint32_t height;
	uint32_t format;
	uint32_t flags;
} aster_surface_desc_t;

typedef struct {
	uint32_t size;
	uint32_t version;
	void     *ptr;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t format;
	uint32_t flags;
} aster_surface_buffer_t;

int32_t aster_surface_create(const aster_surface_desc_t *desc,
						       uint32_t desc_size, aster_handle_t *out);
int32_t aster_surface_destroy(aster_handle_t surface);
int32_t aster_surface_acquire(aster_handle_t surface,
							 aster_surface_buffer_t *buf, uint32_t buf_size);

int32_t aster_surface_present(aster_handle_t surface);

int32_t aster_surface_flush(aster_handle_t surface, const void *src,
						    uint32_t x, uint32_t y, uint32_t w,
						    uint32_t h, uint32_t src_pitch);
```

### input.h

```c
#define ASTER_INPUT_TOUCH    1u
#define ASTER_INPUT_KEYS     2u

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t kinds;
	uint32_t max_points;
	uint32_t has_gesture;
} aster_input_info_t;

int32_t aster_input_get_info(aster_input_info_t *info, uint32_t info_size);

int32_t aster_input_touch(uint32_t points_offset, uint32_t max_points);

typedef struct {
	uint32_t x;
	uint32_t y;
	uint32_t action;
} aster_touch_point_t;

int32_t aster_input_keys_state(uint32_t codes_offset,
							   uint32_t max_codes);
```

### timer.h

```c
int32_t aster_timer_create(uint32_t period_ms, uint32_t repeat,
						   uint32_t user, aster_handle_t *out);
int32_t aster_timer_destroy(aster_handle_t timer);
```

### fs.h

```c
#define ASTER_FS_O_RDONLY 0x01u
#define ASTER_FS_O_WRONLY 0x02u
#define ASTER_FS_O_RDWR   0x03u
#define ASTER_FS_O_CREAT  0x10u
#define ASTER_FS_O_TRUNC  0x20u
#define ASTER_FS_O_APPEND 0x40u

#define ASTER_FS_SEEK_SET 0
#define ASTER_FS_SEEK_CUR 1
#define ASTER_FS_SEEK_END 2

typedef struct {
	uint32_t size;
	uint32_t version;
	uint64_t filesize;
	uint32_t mode;
	uint32_t reserved;
} aster_stat_t;

#define ASTER_STAT_DIR 1u

int32_t aster_fs_open(const char *path, uint32_t path_len, uint32_t flags,
			 aster_handle_t *out);
int32_t aster_fs_read(aster_handle_t file, void *dst, uint32_t len);
int32_t aster_fs_write(aster_handle_t file, const void *src, uint32_t len);
int32_t aster_fs_seek(aster_handle_t file, int32_t ofs, uint32_t whence);
int32_t aster_fs_close(aster_handle_t file);
int32_t aster_fs_mkdir(const char *path, uint32_t path_len);
int32_t aster_fs_unlink(const char *path, uint32_t path_len);
int32_t aster_fs_stat(const char *path, uint32_t path_len, aster_stat_t *st,
				uint32_t st_size);

int32_t aster_fs_dir_next(aster_handle_t dir, char *name, uint32_t name_max,
					uint32_t *is_dir);
int32_t aster_fs_opendir(const char *path, uint32_t path_len, aster_handle_t *out);
int32_t aster_fs_closedir(aster_handle_t dir);
```

### kv.h

```c
int32_t aster_kv_set(const char *key, uint32_t key_len,
		       const void *val, uint32_t val_len);

int32_t aster_kv_get(const char *key, uint32_t key_len,
		       void *dst, uint32_t dst_max);
int32_t aster_kv_delete(const char *key, uint32_t key_len);
```

### audio.h

```c
#define ASTER_AUDIO_IDLE    0u
#define ASTER_AUDIO_PLAYING 1u
#define ASTER_AUDIO_PAUSED  2u

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t bits;
	uint32_t flags;
} aster_audio_info_t;

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t state;
	uint32_t bytes_total;
	uint32_t bytes_done;
	uint32_t volume;
	uint32_t msec_done;
	uint32_t msec_total;
} aster_audio_state_t;

int32_t aster_audio_get_info(aster_audio_info_t *info, uint32_t size);

int32_t aster_audio_set_volume(int32_t percent);

int32_t aster_audio_get_volume(void);

int32_t aster_audio_record(uint8_t *pcm, uint32_t size);

int32_t aster_audio_start_file(const char *path, uint32_t path_len);

int32_t aster_audio_effect_load(const char *path, uint32_t path_len);
int32_t aster_audio_effect_play(uint32_t handle);
int32_t aster_audio_effect_unload(uint32_t handle);

int32_t aster_audio_pause(void);
int32_t aster_audio_resume(void);
int32_t aster_audio_stop(void);

int32_t aster_audio_get_state(aster_audio_state_t *st, uint32_t size);

int32_t aster_audio_seek(uint32_t msec);

int32_t aster_audio_get_lyrics(char *buf, uint32_t size);
```

### sensor.h

```c
int32_t aster_accel_read(float *out_xyz, uint32_t size);
int32_t aster_gyro_read(float *out_xyz, uint32_t size);
```

### imu.h

```c
#define ASTER_IMU_ACCEL_VALID (1u << 0)
#define ASTER_IMU_GYRO_VALID  (1u << 1)

typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t flags;
    uint32_t min_interval_ms;
} aster_imu_info_t;

typedef struct {
    uint32_t size;
    uint32_t version;
    uint64_t timestamp_ms;
    uint32_t sequence;
    uint32_t flags;
    float accel[3];
    float gyro[3];
} aster_imu_sample_t;

int32_t aster_imu_get_info(
    aster_imu_info_t *out, uint32_t out_size);

int32_t aster_imu_read(
    aster_imu_sample_t *out, uint32_t out_size);
```

### camera.h

```c
int32_t aster_cam_info(uint32_t *out_w, uint32_t *out_h);

int32_t aster_cam_read(uint8_t *buf, uint32_t size);
```

### img.h

```c
#define ASTER_IMG_FMT_UNKNOWN 0u
#define ASTER_IMG_FMT_BMP     1u
#define ASTER_IMG_FMT_JPG     2u
#define ASTER_IMG_FMT_PNG     3u

#define ASTER_IMG_F_UNSUPPORTED (1u << 0)
#define ASTER_IMG_F_TOO_BIG     (1u << 1)

#define ASTER_IMG_F_NEAREST (1u << 0)

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t fmt;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t file_size;
	uint32_t out_w;
	uint32_t out_h;
	uint32_t flags;
	uint32_t reserved;
	char     why[48];
} aster_img_info_t;

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t dst_off;
	uint32_t dst_span;
	uint32_t cap_w;
	uint32_t cap_h;
	uint32_t out_w;
	uint32_t out_h;
	uint32_t flags;
	uint32_t reserved;
} aster_img_render_args_t;

int32_t aster_img_probe(const char *path, uint32_t path_len,
					     aster_img_info_t *out, uint32_t out_size);

int32_t aster_img_render(const char *path, uint32_t path_len,
					       aster_img_render_args_t *args,
					       uint32_t args_size);
```

### appmgr.h

```c
#define ASTER_APPST_STOPPED 0u
#define ASTER_APPST_RUNNING 1u
#define ASTER_APPST_PAUSED  2u
#define ASTER_APPST_CRASHED 3u

typedef struct {
	uint32_t size;
	uint32_t version;
	char     id[64];
	char     name[40];
	char     app_version[24];
	uint32_t state;
	uint32_t has_aot;
	uint32_t flags;
	char     category[16];
	uint32_t has_icon;
} aster_app_info_t;

int32_t aster_appmgr_count(void);

int32_t aster_appmgr_list(uint32_t index,
						 aster_app_info_t *info,
						 uint32_t info_size);

int32_t aster_appmgr_start(const char *id, uint32_t id_len);

int32_t aster_appmgr_uninstall(const char *id,
							   uint32_t id_len);
```

### time.h

```c
#define ASTER_TIME_FLAG_SYNCED (1u << 0)

typedef struct {
	uint32_t size;
	uint32_t version;
	uint64_t unix_ms;
	uint32_t flags;
	int32_t year;
	uint32_t month;
	uint32_t day;
	uint32_t hour;
	uint32_t minute;
	uint32_t second;
	uint32_t weekday;
} aster_time_info_t;

int32_t aster_time_get(
	aster_time_info_t *out, uint32_t size);
```

### wifi.h

```c
#define ASTER_WIFI_SSID_MAX 32
#define ASTER_WIFI_AP_MAX   16
#define ASTER_WIFI_IP_MAX   15

typedef enum {
	ASTER_WIFI_STATE_DISCONNECTED = 0,
	ASTER_WIFI_STATE_DISABLED,
	ASTER_WIFI_STATE_INACTIVE,
	ASTER_WIFI_STATE_SCANNING,
	ASTER_WIFI_STATE_AUTHENTICATING,
	ASTER_WIFI_STATE_ASSOCIATING,
	ASTER_WIFI_STATE_ASSOCIATED,
	ASTER_WIFI_STATE_FOUR_WAY_HANDSHAKE,
	ASTER_WIFI_STATE_GROUP_HANDSHAKE,
	ASTER_WIFI_STATE_COMPLETED,
	ASTER_WIFI_STATE_UNKNOWN,
} aster_wifi_state_t;

typedef enum {
	ASTER_WIFI_OP_NONE = 0,
	ASTER_WIFI_OP_SCAN,
	ASTER_WIFI_OP_CONNECT,
	ASTER_WIFI_OP_DISCONNECT,
	ASTER_WIFI_OP_RECONNECT,
	ASTER_WIFI_OP_FORGET,
} aster_wifi_operation_t;

typedef enum {
	ASTER_WIFI_SECURITY_OPEN = 0,
	ASTER_WIFI_SECURITY_PERSONAL,
	ASTER_WIFI_SECURITY_ENTERPRISE,
	ASTER_WIFI_SECURITY_WEP,
	ASTER_WIFI_SECURITY_OTHER = 255,
} aster_wifi_security_t;

typedef struct {
	uint32_t size;
	uint32_t version;
	char ssid[ASTER_WIFI_SSID_MAX + 1];
	int32_t rssi;
	uint32_t channel;
	uint32_t security;
} aster_wifi_ap_info_t;

typedef struct {
	uint32_t size;
	uint32_t version;
	uint32_t state;
	uint32_t operation;
	uint32_t last_operation;
	int32_t last_result;
	int32_t rssi;
	uint32_t security;
	char ssid[ASTER_WIFI_SSID_MAX + 1];
	char ipv4[ASTER_WIFI_IP_MAX + 1];
} aster_wifi_info_t;

int32_t aster_wifi_scan_start(void);

int32_t aster_wifi_scan_results(
	aster_wifi_ap_info_t *out, uint32_t capacity);

int32_t aster_wifi_connect(
	const char *ssid, uint32_t ssid_len,
	const char *password, uint32_t password_len);
int32_t aster_wifi_disconnect(void);
int32_t aster_wifi_reconnect(void);
int32_t aster_wifi_forget(void);

int32_t aster_wifi_status(
	aster_wifi_info_t *status, uint32_t size);

int32_t aster_wifi_get_saved_ssid(
	char *out, uint32_t capacity);
```

### gfx.h

```c
typedef struct {
	uint32_t w[6];
} aster_gfx_cmd_t;

#define ASTER_GFX_OP(c)   ((c)->w[0] & 0xFFu)
#define ASTER_GFX_P16(v)  ((int16_t)((v) & 0xFFFFu))
#define ASTER_GFX_U16(v)  ((uint16_t)((v) & 0xFFFFu))
#define ASTER_GFX_HI16(v) ((uint16_t)((v) >> 16))
#define ASTER_GFX_PACK(lo, hi) \
	((uint32_t)(((uint32_t)(hi) << 16) | ((uint32_t)(lo) & 0xFFFFu)))

#define ASTER_GFX_P16H(v)	((int16_t)((v) >> 16))
#define ASTER_GFX_HFLAG(v) ((uint32_t)(((v) >> 16) & 1u))
#define ASTER_GFX_LFLAG(v) ((uint32_t)((v) & 1u))
#define ASTER_GFX_SCALE_MASK 0xFFFu
#define ASTER_GFX_ALPHA_MASK 0x1FFu

typedef enum {
	ASTER_GFX_CLEAR   = 0,
	ASTER_GFX_RECT_F  = 1,
	ASTER_GFX_RECT_S  = 2,
	ASTER_GFX_LINE_AA = 3,
	ASTER_GFX_ELLIPSE = 4,
	ASTER_GFX_TRI     = 5,
	ASTER_GFX_GRAD    = 6,
	ASTER_GFX_TEXT    = 7,

	ASTER_GFX_BMP     = 9,
	ASTER_GFX_BMP_S   = 10,
	ASTER_GFX_BMP_A   = 11,
	ASTER_GFX_BMP_SA  = 12,
} aster_gfx_op_t;

int32_t aster_gfx_exec(aster_handle_t surface,
					   const aster_gfx_cmd_t *cmds, uint32_t n,
					   const uint8_t *arena, uint32_t arena_len,
					   uint32_t present_x, uint32_t present_y,
					   uint32_t present_w, uint32_t present_h);

int32_t aster_gfx_fonts_pending(void);

int32_t aster_gfx_text_w(uint32_t px, const char *utf8,
					       uint32_t utf8_len);

int32_t aster_gfx_bmp_load(const char *path, uint32_t path_len,
						   uint32_t cap_w, uint32_t cap_h,
						   uint32_t *out_handle,
						   uint32_t *out_w, uint32_t *out_h);
int32_t aster_gfx_bmp_free(uint32_t handle);
```

### font_metrics.h

```c
#define ASTER_FONT_BAKED_PX { 20, 16 }
#define ASTER_FONT_BAKED_N 2u
#define ASTER_FONT_FACE_METRICS { { 20, 26, 21 }, { 16, 21, 17 } }
#define ASTER_FONT_MAX_BMP 210u
#define ASTER_FONT_MAX_SCRATCH 420u
#define ASTER_FONT_UI_PX 20
#define ASTER_FONT_UI_LINE_H 26
#define ASTER_FONT_UI_ASC 21
#define ASTER_FONT_CAPTION_PX 16
#define ASTER_FONT_CAPTION_LINE_H 21
#define ASTER_FONT_CAPTION_ASC 17
```

## 13. 手册维护约定

此文件是开发知识的唯一正文；Skill 入口只引用本文。维护者在公开 ABI、模板、CLI、资源或目标限制变化时同步更新本文，用正文完整示例实际构建并验证包。不能只更新版本号或把新能力加入标题而缺少类型、参数、错误和生命周期说明。构建验证不能替代设备运行验证。

### 回调期限与故障恢复

配套固件默认限制单次 init/start 为 15 秒、event/pause/resume 为 5 秒、stop/destroy 为 3 秒。计时包含同步 Host 操作；模块加载与 Guest 初始化前字体预热另计。长期工作应分帧或通过定时事件分批执行，避免一次回调等待长录音、网络或大文件处理。期限属于固件策略，可由产品配置调整。

Trap、初始化拒绝及正常退出由 worker 清理资源，记录错误。普通 SDK Xtensa AOT 的纯计算循环没有可靠的异步中断保证；失控回调超过期限时，新固件通过完整芯片复位恢复主页并记录 APP/阶段。复位会重启服务，不保证尚未关闭/同步的数据保留，也不构成强制终止不可信 APP 的安全隔离保证。

Linux/WSL 原生 USB Serial/JTAG 的设备工具关闭 HUPCL，避免关闭串口时释放控制线；端口打开继续保持经过验证的 DTR/RTS 配对状态。CH340 与 Windows 端口策略分别保留原有行为。

### APP 安装和升级失败处理

使用 `aster deploy PACKAGE --target xtensa --port SERIAL_PORT` 安装/升级。先退出运行中或暂停的 APP、返回主页；新版可恢复升级固件在有活动 APP 时拒绝安装并返回 busy。包格式和公开 ABI 不因此改变，已有 `.ast` 可继续使用。

新版固件把新程序及资源写入备用版本槽，逐字节读回校验后才写提交记录，保留当前版本和稳定的 `/data`、`/cache`。APP 仍通过 `/assets` 读取当前版本资源，不需要知道物理槽名；升级无需搬移或重建存档。SD 需要同时容纳当前版本和一个完整新版本；旧布局迁移还保留平铺兼容文件。

安装失败时根据设备工具报告的阶段、操作和路径排查空间、挂载与写权限，修复后可重试。提交记录的同步/关闭失败可能发生在记录已经完整写入之后，因此失败响应不能证明设备一定仍是旧版本；安装器会重新扫描有效版本保持注册表一致。已有旧固件不具备这套恢复保证，需更新匹配固件；此功能的实际 SD/FAT 断电行为仍待统一真机验收。

### SDK 升级与发布兼容性

此前单串口升级只修改 SDK 主机端设备工具及固件传输入口，当时公开 APP ABI 为 3.7.0、包格式仍为 ASTP、传输版本仍为 ASTERFS 3。已有 APP 工程与已安装 `.ast` 不需要重编。发布 SDK 时同步交付新版设备工具、本文和 `.agents/skills/aster-app-development/SKILL.md`；保留二者目录结构，使 Skill 相对链接可用。Windows 发布 EXE 必须从更新后的工具源码重新生成，不能只复制手册并沿用旧设备 EXE。旧固件继续支持显式双串口参数；串口功能升级不要求手工修改 APP 契约或目标文件。

## 绘制提交与性能测量

现有 `aster_ge_*` 命令环已可将一帧图形合并为 `aster_gfx_exec`；避免无意逐条提交。微雪固定全帧矩形场景中，17 次独立提交合并为 1 次后平均耗时约从 362.5 ms 降至 42.5 ms，最终像素一致；这个数值不适用于所有字体、图片和动态场景。详见 [固件图形基准](https://github.com/hello-myj/aster-firmware/blob/main/docs/graphics-benchmark.md)。`app stats` 的 peak 现为有效 Guest 线性内存范围的字节峰值，不包含 AOT、字体或前缓冲，也不是 malloc 当前用量。


## 13. Scene、布局与控件（G7）

### 13.1 适用范围与兼容性

`#include <aster_widgets.h>` 自动包含 `aster_scene.h`、`aster_layout.h`、绘图引擎及精简 libc。只用对象层时包含 `<aster_scene.h>`。构建工具自动链接三个库；旧 APP 未使用这些函数时会被链接器移除。

这是 APP 侧的轻量保留对象层：矩形、图片、单行文本、裁剪容器，加最小行/列/Grid 布局和按钮/滚动。Host 不保存对象树。所有位置按实际 surface 像素计算，未增加缩放/旋转矩阵、富文本、自动换行、列表虚拟化、滚动惯性或任意形状控件。暂不迁移已有播放器/阅读器。

G7实施时C/WASM imports和ABI版本保持3.7.0；本版网络导入另要求3.8。G7 增加的是现有帧事务的**可选能力**，旧版同 ABI Host 可能没有它们。`aster_scene_init` 会查询事务、裁剪、字体 epoch 三项；缺任何一项返回 `ASTER_ENOTSUP`，应提示升级配套固件或模拟器，不能靠忽略错误继续绘制。此工作区能力不等同于此前已发布的 3.7 SDK 成品。

Scene 独占一个普通 RGB565 surface 或 PANEL_CANVAS，使用 `acquire()` 实际返回的 `flags`，不支持 SCALE_FIT 标志。普通 surface 恢复旧/新边界的并集，并按绘制顺序重放相交对象；canvas 每次变更都全屏清除重绘。每个成功帧只 END 一次；静态页不 DRAW/END，但每次 render 仍 QUERY 字体状态。微雪物理面板仍可能全屏发送，不保证 FPS 因 Scene 自动提高。

### 13.2 生命周期、内存与返回值

APP 用 static 或自己分配的存储持有：

```c
static aster_scene_t scene;
static aster_scene_node_t nodes[32];
static aster_widgets_t widgets;
static aster_button_t buttons[10];
static aster_ge_t ge;
static aster_gfx_cmd_t ring[16];
static uint8_t arena[384];
```

不要将这些大数组放在事件回调栈上。Scene 不动态分配对象；容量 1–64，最大嵌套深度 16，按钮容量 1–32。每节点复制最多 127 字节 UTF-8 文本，拒绝 CR/LF；不接受指向外部可变字符串的长期引用。节点 ID 是 `uint32_t`，0 表示隐式根，无效/删除 ID 返回 `ASTER_EBADF`，同一 Scene 生命周期不复用 ID，耗尽返回 `ASTER_ENOSPC`。不可直接修改 `scene`/`nodes` 内部字段，也不要跨 Scene 混用 ID。

以 32 位 APP 的实际 `sizeof` 为准计算预算：固定 Scene 工作区约 5.7 KiB，节点池每节点 248 字节，按钮表每项 12 字节，另计 ge、ring、arena、线性 surface、字体和图片。不存在额外 Host 节点树；帧事务已有的私有暂存仍会占 Host PSRAM。内存需求不是只看节点池。

`aster_scene_render()`：0 没有发布；1 成功发布；负数失败。失败保留脏状态及已发布画面，修复资源/布局后重试；`ASTER_EBUSY` 留到下一帧，不能死循环重试。初始化与 setter 通常成功返回 0。布局先计算再 BEGIN，溢出不会提交半张画面。暂停取消控件捕获；恢复取消捕获并 `aster_scene_invalidate()`。若其他代码直接改了本 surface，先恢复独占并 invalidate；平时不要每帧 invalidate。

图片句柄由 APP 管理，Scene 只借用；`set_image` 的源宽高必须来自加载结果。删除 image 节点不会释放图片，共用图片必须在所有节点停用后再 `aster_gfx_bmp_free()`。destroy 时释放图片和 surface。按钮占两个节点：容器和文本。

### 13.3 完整调用示例

已有工程可使用 `scene-list` 模板；下面也可直接保存为 `src/main.c`。manifest 使用 ABI/abi_min 3.7，`aot=true`，`requires=["display","touch"]`，建议 `initial_memory=524288`、`max_memory=2097152`、`heap=524288`、`stack=32768`；其他 manifest 格式同第 3 节。

```c
#include <aster/aster.h>
#include <aster_widgets.h>
#define CHECK(e) do { int r=(e); if(r<0) return r; } while(0)
static aster_handle_t surf;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[16];
static uint8_t arena[384];
static aster_scene_t scene;
static aster_scene_node_t nodes[16];
static aster_widgets_t widgets;
static aster_button_t buttons[5];
static uint32_t rows[5];
static bool enabled[5];
static uint8_t body;
int aster_app_init(void) {
    aster_surface_desc_t d={.size=sizeof(d),.version=1,.format=ASTER_PIXEL_RGB565};
    CHECK(aster_surface_create(&d,sizeof(d),&surf));
    aster_surface_buffer_t b={.size=sizeof(b),.version=1};
    CHECK(aster_surface_acquire(surf,&b,sizeof(b)));
    aster_ge_init(&ge,surf,b.width,b.height,ring,16,arena,sizeof(arena));
    CHECK(aster_scene_init(&scene,&ge,nodes,16,b.flags,0x0842));
    CHECK(aster_widgets_init(&widgets,&scene,buttons,5));
    body=aster_text_for_display(16,b.width,b.height);
    uint32_t list;
    CHECK(aster_scene_add(&scene,0,ASTER_NODE_CONTAINER,&list));
    CHECK(aster_scene_set_box(&scene,list,(aster_box_t){8,24,b.width-16,b.height-48}));
    CHECK(aster_scene_set_layout(&scene,list,(aster_layout_t){
        .mode=ASTER_LAYOUT_COLUMN,.align=ASTER_ALIGN_STRETCH,.scroll=1,.gap=6}));
    for(unsigned i=0;i<5;i++) {
        CHECK(aster_widgets_button(&widgets,list,(aster_box_t){0,0,b.width-16,56},
            "关闭",body,
            0xffff,0x18e5,0x2a29,&rows[i]));
    }
    aster_app_set_framerate(30);
    return 0;
}
void aster_app_event(const aster_event_t *e) {
    if(e->type==ASTER_EVENT_TOUCH) {
        uint32_t clicked=0;
        int r=aster_widgets_event(&widgets,e,&clicked);
        if(r<0) return;
        for(unsigned i=0;clicked&&i<5;i++) if(rows[i]==clicked) {
            enabled[i]=!enabled[i];
            (void)aster_scene_set_text(&scene,buttons[i].label,enabled[i]?"开启":"关闭",body,0xffff);
        }
    } else if(e->type==ASTER_EVENT_FRAME) {
        int r=aster_scene_render(&scene);
        /* EBUSY: wait for the next FRAME. Other negatives require error handling. */
        if(r<0&&r!=ASTER_EBUSY) aster_log(ASTER_LOG_ERROR,"Scene render failed",19);
    }
}
void aster_app_pause(void) { aster_widgets_cancel(&widgets); }
void aster_app_resume(void) { aster_widgets_cancel(&widgets);aster_scene_invalidate(&scene); }
void aster_app_destroy(void) { if(surf)aster_surface_destroy(surf);surf=0; }
```

按钮默认普通色/按下色会在触摸后恢复；本例只在初始化时建立按钮、不删除重建，因此按钮登记下标保持稳定，可用对应 label ID 更新业务标签。

### 13.4 对象 API

以下参数中 `s` 为 `aster_scene_t *`，`id/parent` 为 `aster_node_id_t`，`b` 为 `aster_box_t {int32_t x,y,w,h;}`。坐标 -32768…32767，宽高 0…32767；实际 surface 像素数仍受帧事务上限限制。

```c
int32_t aster_scene_init(aster_scene_t *s,aster_ge_t *ge,aster_scene_node_t *pool,
    uint32_t capacity,uint32_t surface_flags,uint16_t background);
int32_t aster_scene_add(aster_scene_t *s,aster_node_id_t parent,uint32_t kind,aster_node_id_t *out);
int32_t aster_scene_remove(aster_scene_t *s,aster_node_id_t id);
int32_t aster_scene_set_box(aster_scene_t *s,aster_node_id_t id,aster_box_t b);
int32_t aster_scene_set_text(aster_scene_t *s,aster_node_id_t id,const char *text,uint8_t px,uint16_t color);
int32_t aster_scene_set_color(aster_scene_t *s,aster_node_id_t id,uint16_t color,bool background);
int32_t aster_scene_set_image(aster_scene_t *s,aster_node_id_t id,uint32_t handle,uint32_t w,uint32_t h,uint16_t scale256);
int32_t aster_scene_set_visible(aster_scene_t *s,aster_node_id_t id,bool visible);
int32_t aster_scene_set_opacity(aster_scene_t *s,aster_node_id_t id,uint8_t opacity);
int32_t aster_scene_set_z(aster_scene_t *s,aster_node_id_t id,int32_t z);
int32_t aster_scene_set_layout(aster_scene_t *s,aster_node_id_t id,aster_layout_t layout);
int32_t aster_scene_set_grow(aster_scene_t *s,aster_node_id_t id,uint16_t grow);
int32_t aster_scene_set_scroll(aster_scene_t *s,aster_node_id_t id,int32_t x,int32_t y);
int32_t aster_scene_set_interactive(aster_scene_t *s,aster_node_id_t id,bool enabled);
int32_t aster_scene_set_text_align(aster_scene_t *s,aster_node_id_t id,uint8_t align);
int32_t aster_scene_poll(aster_scene_t *s);
void aster_scene_invalidate(aster_scene_t *s);
int32_t aster_scene_render(aster_scene_t *s);
aster_node_id_t aster_scene_hit_test(const aster_scene_t *s,int32_t x,int32_t y);
int32_t aster_scene_bounds(const aster_scene_t *s,aster_node_id_t id,aster_box_t *out);
```

- kind 为 `ASTER_NODE_CONTAINER/RECT/IMAGE/TEXT`；parent 必须是 0 或容器。删除容器递归删除子树，保留资源所有权。没有重设 parent 接口。
- `box` 相对父容器内容原点，受父 scroll 平移；根直接用 surface 坐标。容器始终裁剪后代，background=true 时填充背景；RECT 始终填充。容器和 RECT 需给非零尺寸。TEXT 零宽/高取测量尺寸，IMAGE 零宽/高取缩放后尺寸。图片有显式 box 时只改变裁剪框，不额外缩放。
- `set_image` 使用已有 BMP handle，scale256 范围 8…4095，256=1倍。opacity 为 0…255，仅 IMAGE 支持中间值，其他类型只支持 0/255。0 或隐藏节点不参与布局和绘制；普通 surface 会恢复旧背景。容器中间透明度返回 ENOTSUP，不实现组透明度。
- 同级 z 小的先绘制，z 相同按创建 ID；容器子树整体位于该容器的同级绘制位置。布局顺序始终按创建 ID，与 z 无关。
- 文本原生字号经 `aster_text_snap` 选取；单行在 box 内垂直居中，text_align=0左/1中/2右，超出 box 裁剪，不省略、不换行。文本、字号、box 尺寸或字体 epoch 改变失效测量；只平移文字或改文字颜色复用测量。几何布局仅在几何相关属性变化时重算。
- poll 成功返回0无字体变化、1变化，负数失败；render 内也会 poll。字体就绪的 epoch 不依赖一次性的全局 repaint ACK，因此无关绘制不会吞掉重绘机会。
- hit_test 返回最后成功发布帧中最上面的 interactive 节点，未命中0；bounds 返回该帧被裁剪后的矩形。输入不会提前使用尚未成功提交的布局。

### 13.5 最小布局与控件 API

```c
typedef struct {int32_t w,h;uint16_t grow;} aster_layout_item_t;
typedef struct {
    uint8_t mode,align,columns,scroll;
    uint16_t padding,gap;
} aster_layout_t;
int32_t aster_layout_compute(const aster_layout_t *layout,int32_t w,int32_t h,
    const aster_layout_item_t *items,uint32_t count,aster_box_t *out,int32_t *content_w,int32_t *content_h);
int32_t aster_widgets_init(aster_widgets_t *w,aster_scene_t *s,aster_button_t *buttons,uint32_t capacity);
int32_t aster_widgets_button(aster_widgets_t *w,aster_node_id_t parent,aster_box_t box,
    const char *label,uint8_t px,uint16_t fg,uint16_t normal,uint16_t pressed,aster_node_id_t *out);
int32_t aster_widgets_event(aster_widgets_t *w,const aster_event_t *event,aster_node_id_t *clicked);
void aster_widgets_cancel(aster_widgets_t *w);
```

mode=`ASTER_LAYOUT_MANUAL(0)/ROW(1)/COLUMN(2)/GRID(3)`；独立 `layout_compute` 只接受后三种，MANUAL 是 Scene 的直接坐标模式。align=`ASTER_ALIGN_START(0)/CENTER(1)/END(2)/STRETCH(3)`，padding 为四边相同内边距，gap 为项目间距。

Row/Column 的主轴按首选尺寸排列，grow 按权重分配剩余空间，不自动缩小；副轴为0或STRETCH时填满。Grid 要求columns=1…64，各列等分可用宽度，余数从左到右分配，每行高度取该行最大首选高；grow在Grid忽略。Grid不实现跨行/跨列。尺寸为0的容器不自动由子内容撑开，需显式指定。列表就是COLUMN容器；没有单独 list 对象。

scroll=0 时溢出返回 ENOSPC；scroll=1 允许主轴（Grid为竖轴）内容超过视口，副轴溢出仍报错。失败不修改独立计算输出，也不发布 Scene 新布局。程序可调整尺寸/间距/scroll 后重试。set_scroll 只接受0…32767，不自动钳制到内容；widgets拖动会钳制，并裁剪视口外内容。

widgets 只消费原有 APP 坐标 TOUCH 事件，单指捕获，默认移动超过8像素取消点击，DOWN 与 UP 命中同一按钮才返回1并写clicked；无点击返回0，错误负数。滚动选择触点下最内层可滚动容器，不实现嵌套滚动接力或惯性。pause/resume调用cancel，避免系统手势接管后残留按下状态。按钮创建时的标签框来自初始 box；后续改变按钮尺寸时应相应更新 label box，或重建按钮，不能假定控件提供完整响应式排版。可保存 `buttons` 返回登记中的 label ID 后用 setter 更新，但删除/再建按钮会压缩登记表，不要长期缓存表下标。

### 13.6 Host 协议与验证边界

事务 QUERY.flags 按位检查：SUPPORTED=1、CLIP_SUPPORTED=2、FONT_EPOCH_SUPPORTED=4。具有 FONT_EPOCH 时 QUERY.frame_id 为只读字体位图 generation；BEGIN.frame_id 仍为事务令牌。可选 SET_CLIP action=4 使用 32字节 `{size=32,version=1,action,frame_id,x,y,w,h}`，沿用现有 control 调用；只允许非空且位于 surface 内的矩形，失效时污染当前帧，BEGIN 重置全屏裁剪。PANEL_CANVAS 首先 full CLEAR 后才能缩小裁剪。普通 APP 使用 Scene 即可，不需要自己编码协议。

源码测试覆盖旧/新区域恢复、alpha/裁剪、删除和 ID、失败帧原子性、字体 epoch、静态无提交、布局溢出恢复、按钮防误触；模拟器集成用真实构建 APP 覆盖 240×320、320×480。实体画面和触摸仍应按对应固件验收记录确认，不能将模拟器成功等同于实机成功。


## 14. 字体与图片质量（G8）

### 14.1 原生文字与背景

保持设计已有的12/14/16等原生小字号。`aster_text_snap` 和实际字体度量来自配套契约；0选默认UI字体，不存在“超出4px改用运行时TTF”的后备。当前固件使用SD原生cpfont，图像不缩放，缺资源时应修复字体文件。位置和基线用整数像素，静止文字不要先画入小缓存再放大。Scene平移文字复用测量，文本/字号/尺寸/字体状态变化才失效。

G8的APP/GFX与Pretty Poly使用相同RGB565混合：每个5/6位原生通道按 `(背景*(255-a)+前景*a+127)/255` 舍入一次。相同前景/背景在任何覆盖率下保持相同，结果不会越过两端颜色。保留cpfont的16级覆盖率；没有改成二值字形，也没有增加字号。配套系统资源的12px字库采用原生尺寸自动hinting，改善细笔画亮暗不均；其他字号资源保持原样，全部字宽、行高和基线不变。它修正量化偏差，不承诺增加屏幕物理分辨率。

TEXT指定bg时，每个字形遮罩像素以所给bg为底混合，重复绘制不会继续混入旧颜色；NOBG仍与当前目标像素混合。bg不等于清除整行：更换长度不同的标签应先清除旧区域，或使用Scene恢复背景。避免在未恢复背景的同一位置反复以NOBG叠加同一抗锯齿字。

当前SD字形读取/预热是同步的，无后台排队；冷字的SD开销会落在请求调用中。页面init期间可先用 `aster_gfx_text_w(px,text,len)` 测量将要显示的常用文本，随后复用结果；不要每帧重复测量、每次切换都预热整份字符表。返回/覆盖后按Scene恢复规则重绘。后台字体资源可用性变化仍由字体epoch通知。

固件shell `aster sys fonts` 提供各字号缓存glyphs/bitmap/alloc，以及累计hits/misses/filled/evictions、SD reads/bytes、成功冷读load_us/max_load_us和warm_us/warm_calls。命中率以可加载字的缓存查询为口径，容量未变；这些是自启动累计的32位计数，做差时考虑回绕。load_us仅统计成功的同步冷加载，不是异步排队耗时；先记录前后快照，再分析命中率与预算。不要仅为降低一次冷启动时间扩大缓存。

### 14.2 按实际尺寸生成素材

优先从原图一次生成实际显示尺寸：像素画保留调色板和最近邻，照片/连续色图离线平滑缩小。不要把缩略图再次放大当原图使用。GFX运行时scale保持最近邻；需要平滑的小图，应先离线生成目标尺寸并以scale=256绘制。launcher的旋转/缩放卡片保留已有设计和插值；不为追求静止锐度删除动画。

可选素材工具（独立Python环境即可，无需固件环境或芯片SDK）：

```text
python -m pip install -r /absolute/path/to/aster-sdk/requirements-assets.txt
python /absolute/path/to/aster-sdk/tools/prepare_image.py original.png images/icon.bmp --width 32 --height 32 --kind pixel
python /absolute/path/to/aster-sdk/tools/prepare_image.py original.jpg images/cover.png --width 160 --height 120 --kind photo
python /absolute/path/to/aster-sdk/tools/prepare_image.py transparent.png images/badge.bmp --width 32 --height 32 --kind photo --background "#101820"
```

输出为保留宽高比后适配目标框的RGB图片，可能小于框尺寸；打印实际尺寸。默认拒绝放大，确有需求可加 `--allow-upscale`。不会覆盖原图。photo用Lanczos，透明图片先以预乘颜色重采样，再按显式背景合成，避免不可见RGB形成色边。普通APP构建和部署不依赖Pillow；便携SDK未增加图片处理EXE命令，只有可选脚本。

该工具不引入图片独立alpha契约。GFX既有0x0000仍是透明键，真实黑色仍可能成为透明区域；PNG的alpha在固件解码时合成到黑色，也不会保留为独立alpha。需要独立alpha的launcher图标继续使用既有ASTI RGB565A8与 `icon_convert.py`；不要把为固定底色预合成的图标再放到任意底色上。具体资源路径及app.toml assets配置同第7节。

### 14.3 模拟与真机验收

模拟器BMP命令按固件相同的RGB565通道混合、色键、裁剪和精确最近邻采样；PNG独立alpha先合成到黑色，fit尺寸采用固件的四舍五入规则。固定原生RGB565图片及采样/混合参考可逐像素比较。运行时不同解码器缩小照片的滤波结果不保证一致，离线预处理到相同尺寸可以避免二次采样差异。

文字的主机系统字体/光栅器与真机cpfont不同，不能用整页文字零像素差证明一致。检查字号选择、基线/边界和覆盖率混合分别通过；对固定同一A8遮罩可要求混合逐像素一致，不同光栅器的字形外观仍需要实体确认，不以任意放宽容差代替检查。G8具体验收状态见固件随源码的验证记录，不复用上一版本的实体结论。


## 15. TCP/UDP 网络通信（ABI 3.8）

以下为工作区开发版，未作为Release发布；先前3.7成品不提供这些导入。


`app.toml`设置`abi="3.8.0"`、`abi_min="3.8"`、`requires=["network"]`、`permissions=["net"]`。`wifi`权限只用于修改无线连接，不隐式授予网络通信。系统设置先联网，网络接口不会扫描、配网或重连路由器。

```bash
python tools/aster.py build apps/network-echo --target xtensa
python tools/aster.py verify apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa
python tools/aster.py deploy apps/network-echo/dist/xtensa/aster.network.echo.ast --target xtensa --port auto --run
```

电脑运行`node tools/network_echo_server.cjs`。真机与电脑连接同一局域网，并把示例app.toml的`NETWORK_HOST`改为电脑的局域网IPv4。WSL NAT环境可以在Windows运行Node服务器、由WSL通过串口部署；连接目标必须是开发板能访问的Windows局域网地址。

模拟器保持127.0.0.1，运行`node src/cli.js APP.ast --network`，或Windows模拟器带相同参数。`--wifi`只模拟无线状态，不启用真实socket；`--network`启用宿主机实际IPv4连接。真实网络不能与确定性replay同时使用。模拟器复现两槽和数据队列等API限制，不模拟ESP32无线时延、信号、电源或两套堆占用。

### 接口规则

包含`<aster_imports.h>`即可使用`aster_network_get_info/open/send/recv/status/cancel/close`。完整签名、结构和示例见本节下文。所有操作非阻塞；在FRAME或TIMER事件轮询，EAGAIN留到后续事件重试，不能忙循环等待。

- `get_info`为只读快照，返回本机IPv4提示和连接/队列限额，不创建socket或扫描/联网。flags分别为PRESENT=1、UP=2、CARRIER=4、IPV4=8；UP仅代表接口启用，ESP载波标志可能在未关联时保留，不能据此证明关联或互联网可达。结构44字节，锁忙返回EAGAIN；模拟器地址是本机候选地址，不承诺与目标路由一致。
- IPv4单播，DNS域名或数字IPv4；host是1..253字节ASCII明确长度，不含NUL，端口1..65535。
- TCP打开异步返回正句柄；超时1..30000ms，涵盖DNS和连接。UDP打开使用空host、0长度、0端口、0超时，等READY再发包。
- 两个全局连接名额，APP与CLI共用；APP不能读取或关闭其他会话/CLI句柄。每次启动创建新会话。关闭立即使句柄失效，后台完成回收前可能暂占名额，ENOSPC表示资源准入失败。
- 单次IO最多2048字节，UDP最多1200字节，收发队列各两包。TCP返回入队字节数，必须继续发送未接受后缀；UDP原子排队，满队列EAGAIN。
- 发送不是送达确认。TCP无进展5秒超时；UDP自入队起5秒（含DNS）。示例对二进制回包使用轮次号、每阶段最多3次超时重试；空包不能携带唯一轮次，不作为可靠消息ACK。自己设计消息边界、序号及重试规则，避免对不可重试操作盲目重复。
- 收取UDP容量不足返回EMSGSIZE，packet.length给出所需长度，原包保留。packet输出必须有效且与数据区不重叠，先验证全部输出再消费数据。空包是合法长度0，不是断开；无包为EAGAIN。
- TCP已缓存数据先于EOF/取消/错误返回；读到0是EOF，此后不能继续发送（EPIPE），本版不承诺半关闭后继续写。
- cancel丢弃未发输出、保留已收输入与终态；close立即失效，APP退出、初始化失败或trap时自动对整个会话执行关闭。无需为清理申请wifi权限。暂停时连接保持活动，应用应考虑暂停期间有限RX队列可能满。
- 输出结构必须传完整v1大小；更大size允许但只写v1前缀，尾部不改；size/version为输出值，不要求预填。invalid/foreign handle为EBADF，短结构/越界/溢出/重叠输出为EINVAL；不会把Zephyr errno泄漏给APP。

新增稳定错误：ECANCELLED=-19、EMSGSIZE=-20、EPIPE=-21、EDNS=-22、ECONNRESET=-23、ECONNREFUSED=-24、ENETUNREACH=-25。连接超时仍为ETIMEDOUT=-8，队列/锁忙重试为EAGAIN=-15。状态字段phase区分socket、DNS、connect、TX、RX、cancel。不要用负数绝对值解释POSIX errno，也不要用发送入队量推断实际回包量。

TCP服务私有缓冲每槽4KiB PSRAM，UDP每槽6568字节（Xtensa构建），另有常驻4KiB内部RAM线程栈；底层协议栈还占系统内存。不要按模拟器进程占用推算硬件APP可用内存，也不要将这些数字当作未来TLS预算。

当前两板固件另有共享的底层协议固定池：数据分片256字节，RX24片、TX32片，net_pkt描述符RX/TX各8个。两板ELF中的数据与net_buf数组合计15904字节PSRAM；这部分由系统与APP共用，不能算成每个APP的私有配额。两连接准入不等于系统协议资源永远充足，APP仍需处理底层资源不足和UDP有限重试。诊断采样器不包含在日常固件中。


### 完整公开声明

以下声明已由umbrella头引入，业务代码只需包含`<aster_imports.h>`，不要重复定义。

```c
/* SPDX-License-Identifier: MIT
 * ABI 3.8: bounded asynchronous IPv4 TCP/UDP. Permission: "net";
 * requires: "network". Does not grant Wi-Fi configuration or auto-reconnect.
 */
#ifndef ASTER_NETWORK_H
#define ASTER_NETWORK_H
#include "types.h"
#define ASTER_NETWORK_TCP 1u
#define ASTER_NETWORK_UDP 2u
#define ASTER_NETWORK_HOST_MAX 253u
#define ASTER_NETWORK_IO_MAX 2048u
#define ASTER_NETWORK_UDP_MAX 1200u
#define ASTER_NETWORK_SLOTS 2u /* shared by all APPs and diagnostic CLI */
#define ASTER_NETWORK_OPENING 1u
#define ASTER_NETWORK_RESOLVING 2u
#define ASTER_NETWORK_CONNECTING 3u
#define ASTER_NETWORK_READY 4u
#define ASTER_NETWORK_EOF 5u
#define ASTER_NETWORK_FAILED 6u
#define ASTER_NETWORK_CANCELLED 7u
#define ASTER_NETWORK_PHASE_SOCKET 1u
#define ASTER_NETWORK_PHASE_DNS 2u
#define ASTER_NETWORK_PHASE_CONNECT 3u
#define ASTER_NETWORK_PHASE_TX 4u
#define ASTER_NETWORK_PHASE_RX 5u
#define ASTER_NETWORK_PHASE_CANCEL 6u

#define ASTER_NETWORK_IF_PRESENT 1u
#define ASTER_NETWORK_IF_UP 2u
#define ASTER_NETWORK_IF_CARRIER 4u /* raw driver flag, not proof of association */
#define ASTER_NETWORK_IF_IPV4 8u

typedef struct {
    uint32_t size, version; /* returned sizeof(struct), 1 */
    uint32_t flags;
    char ipv4[16]; /* local IPv4 hint, empty without a preferred address */
    uint32_t max_connections, tcp_buffer_bytes, udp_payload_max, udp_queue_depth;
} aster_network_info_t;
/* Read cached interface/address state and service limits; no scan/connect or
 * internet probe. A local address does not prove reachability. UP means the
 * interface is enabled; ESP carrier can remain set while unassociated.
 * Returns OK with zero flags when no interface exists. size >=sizeof(info).
 */
ASTER_I("aster_network_get_info") int32_t aster_network_get_info(
    aster_network_info_t *info, uint32_t size);

typedef struct {
    uint32_t size, version; /* returned sizeof(struct), 1 */
    uint32_t kind, state, phase;
    int32_t error; /* stable ASTER_E..., never platform errno */
    uint32_t tx_queued, rx_queued, dropped, local_port;
    uint64_t tx_bytes, rx_bytes;
} aster_network_status_t;
typedef struct {
    uint32_t size, version; /* returned sizeof(struct), 1 */
    uint32_t length; /* received length; required length on EMSGSIZE */
    uint32_t port; /* UDP source port on successful receive; TCP: 0 */
    char ipv4[16]; /* UDP dotted IPv4 on successful receive; TCP/error: empty */
} aster_network_packet_t;

/* All calls return promptly. EAGAIN means retry on a later event/timer.
 * Open returns a positive opaque handle, or ASTER_E.... Host strings use an
 * explicit length (no NUL, ASCII hostname/IPv4, 1..253); IPv6/multicast are
 * unsupported. TCP port 1..65535, timeout 1..30000 includes DNS+connect.
 * UDP open requires host=NULL, host_len=port=timeout_ms=0. No listener API.
 * Cancelled/failed/EOF handles retain their quota until close or APP exit.
 */
ASTER_I("aster_network_open") int32_t aster_network_open(
    uint32_t kind, const char *host, uint32_t host_len,
    uint32_t port, uint32_t timeout_ms);
/* length <=2048, UDP <=1200. TCP: host=NULL, host_len=port=0; returns
 * accepted byte count (may be partial). UDP: destination required; atomic
 * datagram enqueue including zero-length. data=NULL is allowed only at zero
 * length. Acceptance does not prove wire delivery. TCP output expires after
 * 5s without progress; UDP after 5s total including DNS/queue residence.
 * UDP TX/RX queue depth=2; oversized/full incoming packets drop and count.
 */
ASTER_I("aster_network_send") int32_t aster_network_send(
    uint32_t handle, const void *data, uint32_t length,
    const char *host, uint32_t host_len, uint32_t port);
/* capacity <=2048; zero allowed only for UDP (data may be NULL). packet is
 * mandatory, size >=sizeof(aster_network_packet_t); validates all outputs
 * before consuming data. No overlapping data/packet ranges. Returns byte
 * count, EAGAIN, or stable error. UDP EMSGSIZE preserves the packet and sets
 * required length. TCP 0 means EOF; UDP 0 means an empty datagram. Buffered
 * input precedes EOF/cancel/error. TCP writes after observed EOF fail EPIPE;
 * native Zephyr cannot continue writing after a peer half-close.
 */
ASTER_I("aster_network_recv") int32_t aster_network_recv(
    uint32_t handle, void *data, uint32_t capacity,
    aster_network_packet_t *packet, uint32_t size);
/* size >=sizeof(status); larger buffers accepted, tail left untouched. */
ASTER_I("aster_network_status") int32_t aster_network_status(
    uint32_t handle, aster_network_status_t *status, uint32_t size);
/* Cancel retains buffered input/status, discards unsent data. Close makes
 * the handle invalid immediately; worker cleanup may briefly retain quota.
 * APP stop/trap/init failure automatically closes the entire session.
 */
ASTER_I("aster_network_cancel") int32_t aster_network_cancel(uint32_t handle);
ASTER_I("aster_network_close") int32_t aster_network_close(uint32_t handle);
#endif
```

### 完整事件驱动示例

创建`app.toml`：

```toml
[app]
id = "aster.network.echo"
name = "Network Echo"
description = "校验 TCP 和 UDP 的实际收发内容。"
version = "1.0.0"
abi = "3.8.0"
abi_min = "3.8"
aot = true
stack = 16384
initial_memory = 262144
max_memory = 524288
heap = 65536
requires = ["network"]
permissions = ["net"]
sources = ["src/main.c"]

[build]
# Replace with the computer's LAN IPv4 for hardware. Loopback is for simulator.
defines = ['NETWORK_HOST="127.0.0.1"', 'NETWORK_TCP_PORT=17780', 'NETWORK_UDP_PORT=17781']
```

创建`src/main.c`：

```c
/* SPDX-License-Identifier: MIT: asynchronous event-driven TCP + UDP echo. */
#include <aster/aster.h>
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#ifndef NETWORK_HOST
#define NETWORK_HOST "127.0.0.1"
#endif
#ifndef NETWORK_TCP_PORT
#define NETWORK_TCP_PORT 17780
#endif
#ifndef NETWORK_UDP_PORT
#define NETWORK_UDP_PORT 17781
#endif
#ifndef NETWORK_REPEAT
#define NETWORK_REPEAT 1
#endif
#ifndef NETWORK_HOLD
#define NETWORK_HOLD 0
#endif
static const char host[] = NETWORK_HOST;
static int32_t tcp, udp;
static uint32_t sent, received, udp_stage, rounds, started, closing;
static uint32_t info_pending=1;
static uint32_t udp_wait, udp_retries, retries_total, stale;
static uint8_t body[1200], input[2048];
static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[12];
static uint8_t arena[256];
static void draw(void)
{
    if(!surface)return;
    char label[80];
    aster_ge_clear(&ge,ASTER_RGB565(16,20,32));
    aster_ge_text(&ge,12,34,20,ASTER_WHITE,0,"Network Echo");
    aster_snprintf(label,sizeof(label),"TCP %u/512  UDP %u",received,udp_stage);
    aster_ge_text(&ge,12,68,14,ASTER_CYAN,0,label);
    aster_snprintf(label,sizeof(label),"Rounds %u",rounds);
    aster_ge_text(&ge,12,94,14,ASTER_WHITE,0,label);
    aster_ge_rect(&ge,(aster_time_now_ms()/15)%(ge.w-24),124,24,24,ASTER_GREEN);
    int rc=aster_ge_present(&ge);
    if(rc<0){aster_logf(ASTER_LOG_ERROR,"NETAPP draw=%d",rc);aster_app_exit(1);}
}
static void fail(const char *where, int32_t code)
{
    aster_logf(ASTER_LOG_ERROR,"NETAPP FAIL %s code=%d",where,code);
    aster_app_exit(1);
}
int aster_app_init(void)
{
    for (uint32_t i=0;i<sizeof(body);++i) body[i]=(uint8_t)i;
    started=aster_time_now_ms();
    if(aster_system_has_feature(ASTER_FEATURE_DISPLAY)>0){
        aster_surface_desc_t desc={sizeof(desc),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
        aster_surface_buffer_t buffer={.size=sizeof(buffer),.version=1};
        int rc=aster_surface_create(&desc,sizeof(desc),&surface);
        if(rc)return rc;
        rc=aster_surface_acquire(surface,&buffer,sizeof(buffer));if(rc)return rc;
        if(buffer.width<=24)return ASTER_ENOTSUP;
        aster_ge_init(&ge,surface,buffer.width,buffer.height,commands,12,arena,sizeof(arena));
    }
    return ASTER_OK;
}
static int ready(int32_t handle)
{
    aster_network_status_t status;
    int32_t rc=aster_network_status(handle,&status,sizeof(status));
    if(rc==ASTER_EAGAIN)return 0;
    if(rc){fail("status",rc);return 0;}
    if(status.state==ASTER_NETWORK_FAILED||status.state==ASTER_NETWORK_CANCELLED){fail("transport",status.error);return 0;}
    return status.state==ASTER_NETWORK_READY||status.state==ASTER_NETWORK_EOF;
}
void aster_app_event(const aster_event_t *event)
{
    if(event->type!=ASTER_EVENT_FRAME)return;
    draw();
    if(info_pending){
        aster_network_info_t info;int result=aster_network_get_info(&info,sizeof(info));
        if(result==ASTER_EAGAIN)return;
        if(result){fail("interface info",result);return;}
        aster_logf(ASTER_LOG_INFO,"NETAPP interface=%s flags=%u slots=%u",info.ipv4,info.flags,info.max_connections);
        info_pending=0;
    }
    if((uint32_t)(aster_time_now_ms()-started)>15000){fail("deadline",ASTER_ETIMEDOUT);return;}
    uint32_t now=aster_time_now_ms();
    if((udp_stage==1||udp_stage==2||udp_stage==4) && (uint32_t)(now-udp_wait)>1500){
        if(udp_retries==3){fail("udp retries",ASTER_ETIMEDOUT);return;}
        ++udp_retries;++retries_total;
        udp_stage=udp_stage==4?3:0;
        aster_logf(ASTER_LOG_WARN,"NETAPP udp retry=%u total=%u",udp_retries,retries_total);
    }
    if(closing){
        if(tcp>0){int rc=aster_network_close(tcp);if(rc==ASTER_EAGAIN)return;tcp=0;}
        if(udp>0){int rc=aster_network_close(udp);if(rc==ASTER_EAGAIN)return;udp=0;}
        aster_logf(ASTER_LOG_INFO,"NETAPP PASS rounds=%u retries=%u stale=%u",rounds,retries_total,stale);aster_app_exit(0);return;
    }
    if(!tcp){int rc=aster_network_open(ASTER_NETWORK_TCP,host,sizeof(host)-1,NETWORK_TCP_PORT,5000);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("tcp open",rc);return;}tcp=rc;aster_logf(ASTER_LOG_INFO,"NETAPP tcp=%d",tcp);}
    if(!udp){int rc=aster_network_open(ASTER_NETWORK_UDP,NULL,0,0,0);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("udp open",rc);return;}udp=rc;aster_logf(ASTER_LOG_INFO,"NETAPP udp=%d",udp);}
    if(!ready(tcp)||!ready(udp))return;
    if(sent<512){int rc=aster_network_send(tcp,body+sent,512-sent,NULL,0,0);if(rc==ASTER_EAGAIN)return;if(rc<0){fail("tcp send",rc);return;}sent+=rc;}
    if(received<512){aster_network_packet_t packet;int rc=aster_network_recv(tcp,input,512-received,&packet,sizeof(packet));if(rc!=ASTER_EAGAIN){if(rc<=0||memcmp(input,body+received,rc)){fail("tcp bytes",rc);return;}received+=rc;}}
    if(udp_stage==0){
        /* Correlate retried binary datagrams; never count an old echo as new. */
        body[1196]=(uint8_t)rounds;body[1197]=(uint8_t)(rounds>>8);
        body[1198]=(uint8_t)(rounds>>16);body[1199]=(uint8_t)(rounds>>24);
        int rc=aster_network_send(udp,body,1200,host,sizeof(host)-1,NETWORK_UDP_PORT);if(rc==ASTER_EAGAIN)return;if(rc!=1200){fail("udp send",rc);return;}udp_stage=1;udp_wait=now;}
    if(udp_stage==1){aster_network_packet_t packet;int rc=aster_network_recv(udp,input,1199,&packet,sizeof(packet));if(rc==0){++stale;return;}if(rc!=ASTER_EAGAIN){if(rc!=ASTER_EMSGSIZE||packet.length!=1200){fail("udp short",rc);return;}udp_stage=2;}}
    if(udp_stage==2){aster_network_packet_t packet;int rc=aster_network_recv(udp,input,sizeof(input),&packet,sizeof(packet));if(rc==ASTER_EAGAIN)return;if(rc!=1200||packet.port!=NETWORK_UDP_PORT){fail("udp bytes",rc);return;}if(memcmp(input+1196,body+1196,4)){++stale;udp_stage=1;return;}if(memcmp(input,body,1200)){fail("udp bytes",rc);return;}udp_retries=0;udp_stage=3;}
    if(udp_stage==3){int rc=aster_network_send(udp,NULL,0,host,sizeof(host)-1,NETWORK_UDP_PORT);if(rc==ASTER_EAGAIN)return;if(rc){fail("udp empty send",rc);return;}udp_stage=4;udp_wait=now;}
    if(udp_stage==4){aster_network_packet_t packet;int rc=aster_network_recv(udp,NULL,0,&packet,sizeof(packet));if(rc==ASTER_EAGAIN)return;if(rc==ASTER_EMSGSIZE){rc=aster_network_recv(udp,input,sizeof(input),&packet,sizeof(packet));if(rc>=0){++stale;return;}if(rc==ASTER_EAGAIN)return;}if(rc){fail("udp empty recv",rc);return;}udp_retries=0;udp_stage=5;}
    if(received==512&&udp_stage==5){
        ++rounds;if(rounds==1||rounds%50==0)aster_logf(ASTER_LOG_INFO,"NETAPP round=%u tcp=512 udp=1200 empty=ok",rounds);
        if(NETWORK_HOLD){started=aster_time_now_ms();udp_stage=0;return;}
        if(rounds>=NETWORK_REPEAT){closing=1;return;}
        /* Reuse UDP for more rounds. TCP EOF is kept until final close. */
        udp_stage=0;started=aster_time_now_ms();
    }
}
void aster_app_stop(void)
{
    /* Best effort only: runtime also closes all session handles on stop/trap. */
    if(tcp>0)(void)aster_network_close(tcp);
    if(udp>0)(void)aster_network_close(udp);
}

void aster_app_resume(void) { started=aster_time_now_ms(); }
```
