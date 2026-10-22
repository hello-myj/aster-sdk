# 连续音效（开发分支）

当前 ABI 为 4.1.0。本页只说明已实现的第一阶段；随 `sdk-v4.1.0-rc.1` 候选版提供。代码位于三个仓库的 `feature/continuous-audio` 分支，完整优化方案尚未完成。

## 工程配置

用到下列任一新导入时，`app.toml` 必须写 `abi = "4.1.0"`、`abi_min = "4.1"`，并声明 `requires = ["audio-out", "audio-effects"]`。加载文件还需要 `permissions = ["fs"]`。保留旧调用的 4.0 应用仍可运行于 4.1 固件；新导入不能只靠 feature 判断来兼容旧固件，因为加载器会先解析导入。

```c
#include <aster/aster.h>

static int32_t engine;

int start_engine(void)
{
    const char path[] = "/assets/audio/engine.wav";
    engine = aster_audio_effect_load(path, sizeof(path)-1);
    if (engine < 0) return engine;
    aster_audio_effect_play_desc_t d = {
        .size = sizeof(d), .version = 1,
        .flags = ASTER_AUDIO_EFFECT_F_LOOP,
        .pitch_q16 = 65536, .gain_q16 = 32768,
        .fade_in_ms = 80
    };
    return aster_audio_effect_play_ex((uint32_t)engine, &d, sizeof(d));
}

int set_engine_speed(uint32_t pitch_q16)
{
    aster_audio_effect_params_t p = {
        .size = sizeof(p), .version = 1,
        .pitch_q16 = pitch_q16, .gain_q16 = 32768, .ramp_ms = 120
    };
    return aster_audio_effect_set_params((uint32_t)engine, &p, sizeof(p));
}

/* Fade this voice without stopping music or other effect handles. */
int stop_engine(void) { return aster_audio_effect_stop((uint32_t)engine, 100); }
/* On APP stop/destroy: stop immediately, unload once, and clear the handle. */
void free_engine(void)
{
    if (engine > 0) {
        (void)aster_audio_effect_stop((uint32_t)engine, 0);
        (void)aster_audio_effect_unload((uint32_t)engine);
        engine = 0;
    }
}
```

启动循环只调用一次。速度或音量发生变化时调用 set_params；不要每帧调用 play_ex，也不要在 FRAME 定时重播短片段。停止后 PCM 仍在缓存；再次 play_ex 从源帧 0 重启。unload 才释放缓存。重播同一句柄替换该声部，不叠加新声部。

## 固定布局与范围

`aster_audio_effect_play_desc_t` 为九个 uint32_t，36 字节：size、version、flags、loop_start_frame、loop_end_frame、crossfade_frames、pitch_q16、gain_q16、fade_in_ms。`aster_audio_effect_params_t` 为五个 uint32_t，20 字节：size、version、pitch_q16、gain_q16、ramp_ms。version 都为 1，调用的 size 参数和结构的 size 字段均必须精确匹配。未知 flags、错误版本或越界参数返回 EINVAL。

- flags 只能为 0 或 ASTER_AUDIO_EFFECT_F_LOOP（1）。
- pitch_q16 范围 32768–131072，即 0.5–2.0 倍；65536 为原速。变速也会改变音高，不是时间拉伸。
- gain_q16 范围 0–65536，即静音到原始增益。0 仍推进相位，不暂停循环。最终混音统一饱和到 PCM16，多路满增益叠加可能削波，应预留余量。
- fade_in_ms、ramp_ms、stop 的 fade_out_ms 范围 0–1000，0 表示立即切换。平滑按音频输出采样推进，不依赖 APP 帧率。
- 非循环时三个 loop/crossfade 字段必须为 0。
- 循环先播放从帧 0 开始的引入段，再循环 `[start,end)`；end=0 表示源文件总帧数。必须 `start < end <= frames`。这里“帧”是一组完整单声道或 L/R 样本，不是字节。
- crossfade_frames 不超过循环长度的一半。尾部与循环头重叠，回绕到 start+crossfade，稳态周期为 end-start-crossfade 帧。因此交叉淡化会缩短循环周期。硬接循环用 0，并确保素材首尾连续。
- set_params 不重置相位；淡出停止期间返回 ESTATE，避免参数更新意外撤销停止。已停止的有效句柄再次 stop 成功。停止/卸载不影响其他声部。

音效 WAV 支持 RIFF PCM16、小端、单声道/立体声、8–48 kHz。支持 LIST/JUNK 等额外块及奇数填充。每个 data 最多 64 KiB，共四个句柄；不支持压缩 WAV、浮点 WAV 或 MP3 音效。单声道会复制到左右声道，按当前输出采样率重采样。背景 MP3 保持原生采样率；`aster_audio_get_info` 仍返回录音/原生 PCM 输入格式，并非当前 I²S 输出格式。

## 生命周期与当前边界

固件 APP 暂停和系统面板打开会冻结 APP 音效相位，恢复后继续；launcher 自身的提示音不受该冻结开关影响。旧 audio_pause/resume 仍控制整个播放会话，旧 audio_stop 仍停止全部播放。APP 退出/异常会回收音效。

背景音乐自然 EOF 时，已运行的音效继续 over-silence。**首版的 start_file、流式音乐启动/关闭和全局 stop 仍沿用旧会话交接；切歌可能停止音效。尚未提供独立 BGM stop，也未保证 SD 阻塞或解码繁忙时无欠载。** 请先启动 BGM，再 play_ex 启动循环；不要用全局 stop 停止单个引擎声。

模拟器实现相同描述校验、时间推进和浏览器循环/变速/淡化。无窗口测试只验证逻辑，不产生声音；浏览器需要启用声音，Web Audio 的重采样和硬件输出不保证与固件逐样本一致。模拟器的 `audio-stream` 仍不可用。

## 示例与验证

```bash
python tools/build.py apps/continuous-audio --target xtensa
```

示例自行在 APP 私有 `/data/loop.wav` 生成约 4 KiB 测试音，无外部音源依赖；以 5 FPS 绘制、音效由后台独立循环。左侧点按在 1x/1.5x 间平滑变速，右侧淡出停止/重新启动。打开系统面板后返回，循环相位应继续；退出后应回收缓存。

已完成：主机 DSP/解析器测试、双板编译、示例 AOT 构建、模拟器无窗口运行及两种分辨率各50轮生命周期检查。微雪已完成600.57秒单声部循环观察，用户确认测试音连续正常；硬件欠载数仍未知。暂停/上下拉、SD压力复合播放、完整资源峰值及LCKFB实机仍待验收。2026-10-22用户要求停止后续测试，完整方案尚未完成。
