/* SPDX-License-Identifier: MIT
 * Star Catch: drag the catcher to collect stars and avoid meteors.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster_text.h>

#define MAX_DROPS 12
#define BACK_STARS 18
#define FACE_PX ((uint32_t)ASTER_TEXT_FACES[0].px)
#define BG       ASTER_RGB565(8, 17, 38)
#define SKY      ASTER_RGB565(31, 51, 83)
#define WHITE    ASTER_RGB565(239, 246, 255)
#define MUTED    ASTER_RGB565(113, 145, 181)
#define GOLD     ASTER_RGB565(255, 207, 74)
#define GOLD_LIT ASTER_RGB565(255, 242, 174)
#define RED      ASTER_RGB565(245, 88, 101)
#define CYAN     ASTER_RGB565(78, 210, 226)

/* Phase 0: waiting, 1: playing, 2: game over. */
static int phase;
static aster_handle_t surface = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[128];
static uint8_t text_arena[256];
static int32_t width, height, catcher_x, catcher_y;
static int left_held, right_held, lives, score;
static uint32_t frames, random_state;

typedef struct {
    int32_t x, y;
    uint8_t kind; /* 0 star, 1 meteor */
    uint8_t speed;
    uint8_t active;
} drop_t;
static drop_t drops[MAX_DROPS];

static uint32_t next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static int32_t clamp_x(int32_t x)
{
    if (x < 28) return 28;
    if (x > width - 28) return width - 28;
    return x;
}

static void reset_game(void)
{
    for (int i = 0; i < MAX_DROPS; ++i) drops[i].active = 0;
    random_state ^= aster_time_now_ms() | 1u;
    catcher_x = width / 2;
    left_held = right_held = 0;
    frames = 0;
    score = 0;
    lives = 3;
    phase = 1;
}

static void spawn_drop(void)
{
    for (int i = 0; i < MAX_DROPS; ++i) {
        if (drops[i].active) continue;
        drops[i].x = 16 + (int32_t)(next_random() % (uint32_t)(width - 32));
        drops[i].y = 33;
        drops[i].kind = (uint8_t)((next_random() % 5u) == 0u);
        drops[i].speed = (uint8_t)(2 + next_random() % 3u + (uint32_t)(score / 80 < 3 ? score / 80 : 3));
        drops[i].active = 1;
        return;
    }
}

static void step_game(void)
{
    if (phase != 1) return;
    ++frames;
    if (left_held != right_held) {
        catcher_x = clamp_x(catcher_x + (left_held ? -7 : 7));
    }
    uint32_t cadence = score >= 160 ? 9u : score >= 80 ? 11u : 14u;
    if (frames % cadence == 0u) spawn_drop();
    for (int i = 0; i < MAX_DROPS; ++i) {
        drop_t *d = &drops[i];
        if (!d->active) continue;
        d->y += d->speed;
        if (d->y >= catcher_y - 10 && d->y <= catcher_y + 10 &&
            d->x >= catcher_x - 29 && d->x <= catcher_x + 29) {
            d->active = 0;
            if (d->kind) --lives;
            else score += 10;
        } else if (d->y > height + 9) {
            d->active = 0;
            if (!d->kind) --lives;
        }
        if (lives <= 0) {
            lives = 0;
            phase = 2;
            break;
        }
    }
}

static void centered_text(int32_t y, uint16_t color, const char *s)
{
    int32_t pixels = aster_gfx_text_w(FACE_PX, s, (uint32_t)strlen(s));
    if (pixels >= 0) {
        aster_ge_text(&ge, (width - pixels) / 2, y, FACE_PX, color,
                      ASTER_GFX_NOBG, s);
    }
}

static void draw_star(int32_t x, int32_t y)
{
    aster_ge_tri(&ge, x, y - 9, x - 7, y + 5, x + 7, y + 5, GOLD);
    aster_ge_tri(&ge, x, y + 9, x - 7, y - 5, x + 7, y - 5, GOLD);
    aster_ge_ellipse(&ge, x, y, 3, 3, GOLD_LIT, 0);
}

static void render(void)
{
    char line[32];
    aster_ge_clear(&ge, BG);
    aster_ge_rect(&ge, 0, 31, (uint32_t)width, 1, SKY);
    for (int i = 0; i < BACK_STARS; ++i) {
        int32_t x = (i * 73 + 19) % width;
        int32_t y = 44 + (i * 47) % (height - 90);
        aster_ge_rect(&ge, x, y, (i % 3 == 0) ? 2u : 1u, 1, SKY);
    }
    for (int i = 0; i < MAX_DROPS; ++i) {
        const drop_t *d = &drops[i];
        if (!d->active) continue;
        if (d->kind) {
            aster_ge_ellipse(&ge, d->x, d->y, 8, 8, RED, 0);
            aster_ge_ellipse(&ge, d->x - 2, d->y - 2, 2, 2, WHITE, 0);
        } else {
            draw_star(d->x, d->y);
        }
    }
    /* A catcher with a bright rim and a visible opening. */
    aster_ge_rect_r(&ge, catcher_x - 28, catcher_y, 56, 14, CYAN, 5, 0);
    aster_ge_rect(&ge, catcher_x - 20, catcher_y + 3, 40, 3, BG);
    aster_ge_rect(&ge, 0, height - 10, (uint32_t)width, 1, SKY);
    aster_snprintf(line, (int)sizeof(line), "SCORE %d", score);
    aster_ge_text(&ge, 7, 22, FACE_PX, WHITE, ASTER_GFX_NOBG, line);
    aster_snprintf(line, (int)sizeof(line), "LIFE %d", lives);
    int32_t tw = aster_gfx_text_w(FACE_PX, line, (uint32_t)strlen(line));
    if (tw >= 0) aster_ge_text(&ge, width - tw - 7, 22, FACE_PX,
                               GOLD, ASTER_GFX_NOBG, line);
    if (phase != 1) {
        int32_t panel_y = height / 2 - 40;
        aster_ge_rect_r(&ge, (width - 210) / 2, panel_y, 210, 78,
                        ASTER_RGB565(17, 34, 58), 9, 0);
        aster_ge_rect_r(&ge, (width - 210) / 2, panel_y, 210, 78,
                        phase == 2 ? RED : CYAN, 9, 1);
        centered_text(panel_y + 30, phase == 2 ? RED : GOLD,
                      phase == 2 ? "GAME OVER" : "STAR CATCH");
        centered_text(panel_y + 57, WHITE,
                      phase == 2 ? "TAP TO RETRY" : "TAP TO START");
    }
    aster_ge_present(&ge);
}

ASTER_APP_EXPORT int aster_app_init(void)
{
    aster_surface_desc_t desc = {
        .size = sizeof(desc), .version = 1, .format = ASTER_PIXEL_NATIVE,
        .flags = ASTER_SURFACE_F_PANEL_CANVAS,
    };
    aster_surface_buffer_t buffer = { .size = sizeof(buffer), .version = 1 };
    int32_t err;
    aster_app_set_framerate(20);
    err = aster_surface_create(&desc, sizeof(desc), &surface);
    if (err != ASTER_OK) return err;
    err = aster_surface_acquire(surface, &buffer, sizeof(buffer));
    if (err != ASTER_OK) {
        aster_surface_destroy(surface);
        surface = ASTER_HANDLE_INVALID;
        return err;
    }
    width = (int32_t)buffer.width;
    height = (int32_t)buffer.height;
    if (width < 220 || height < 180) {
        aster_surface_destroy(surface);
        surface = ASTER_HANDLE_INVALID;
        return ASTER_EINVAL;
    }
    catcher_x = width / 2;
    catcher_y = height - 35;
    lives = 3;
    phase = 0;
    random_state = aster_time_now_ms() ^ 0x51A7C47Cu;
    aster_ge_init(&ge, surface, buffer.width, buffer.height,
                  commands, sizeof(commands) / sizeof(commands[0]), text_arena, sizeof(text_arena));
    return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
    if (surface != ASTER_HANDLE_INVALID) {
        aster_surface_destroy(surface);
        surface = ASTER_HANDLE_INVALID;
    }
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *event)
{
    if (event->type == ASTER_EVENT_TOUCH) {
        if (event->u.touch.action == ASTER_TOUCH_DOWN && phase != 1) {
            reset_game();
        }
        if (phase == 1 && (event->u.touch.action == ASTER_TOUCH_DOWN ||
                           event->u.touch.action == ASTER_TOUCH_MOVE)) {
            catcher_x = clamp_x((int32_t)event->u.touch.x);
        }
    } else if (event->type == ASTER_EVENT_KEY) {
        uint32_t key = event->u.key.code;
        int pressed = event->u.key.pressed != 0;
        if (key == 105u) left_held = pressed;
        if (key == 106u) right_held = pressed;
        if (pressed && phase != 1 && (key == 28u || key == 57u)) reset_game();
    } else if (event->type == ASTER_EVENT_FRAME) {
        step_game();
        render();
    }
}
