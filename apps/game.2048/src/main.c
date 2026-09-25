/* SPDX-License-Identifier: MIT
 * 2048 — swipe to slide tiles, tap overlay to continue / restart.
 *
 * gfx command-ring path, same as snake: the app owns no pixels, it queues
 * 24-byte draw commands and the firmware rasterizes them into the panel
 * canvas. The whole board goes out in ONE aster_gfx_exec per frame, which is
 * also why the frame starts with a full-screen clear (see aster_app_init).
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster/kv.h>
#include <aster/audio.h>
#include <aster_text.h>

#define N     4
#define CELL  52
#define GAP   6
/* Two 20 px baselines, 26 apart, plus their ink. The old HUD 44 held two
 * 12 px lines at baselines 18 and 32; at the single face the first baseline
 * alone has to clear 21 (the ascent) or the ink rows run off the top of the
 * screen, and the second lands at 21 + 26 = 47 with the 'p'-class descenders
 * of nothing here -- digits and capitals sit ON the baseline -- reaching 47.
 * 60 leaves the board's own background (oy - GAP) 8 px below that. */
#define HUD   60

/* Tile number. The ABI exposes no font-metric query, so these are read out
 * of the baked .cpfont's glyph records, not guessed:
 *   every digit advances exactly 12.75 px, and the tallest ('0','3','5'..'9')
 *   is 16 rows of ink whose top sits 15 rows above the baseline, i.e. rows
 *   [base-15, base].
 * Centring that box in the cell is base = (CELL - h)/2 + top = (52-16)/2 + 15
 * = 33, which puts the ink on rows 18..33 and leaves 18 px above, 18 below.
 *
 * The old literal was (CELL*62)/100 = 32 -- the SAME formula evaluated at the
 * 16 px face, where the ink was 13 rows with top 13: (52-13)/2 + 13 = 32. So
 * the constant moved because the face did, not because the layout did. Both
 * numbers come from the bake; re-derive them if it changes again.
 *
 * Width is the tighter constraint now. 12.75 px per digit means a 4-digit
 * value is 51 px in a 52 px cell -- 0.5 px a side -- and 5 digits (65536) is
 * 63 px, which is 6 px wider than the cell and eats the whole 6 px gutter on
 * each side. It still does not touch the neighbour, and the value that
 * overflows is the one you get for merging all sixteen tiles, so it reads as
 * the tile swelling rather than as a bug. 6 digits (131072, only reachable
 * by continuing past a win twice) WOULD overlap its neighbour; the game has
 * no answer for that and neither does this layout. */
#define NUM_PX   ((int)ASTER_TEXT_FACES[0].px)
#define NUM_H    16	/* tallest digit ink height, rows */
#define NUM_TOP  15	/* rows from the baseline up to that ink's top */
#define NUM_BASE ((CELL - NUM_H) / 2 + NUM_TOP)
#define TILE_R   6

/* GAME OVER / YOU WIN panel, horizontally centred at runtime. Two 20 px lines
 * 26 apart: ink rows [27-15, 27] = 12..27 for the title (capitals, no
 * descender, so it stops on its baseline) and 38..53 for the sub ('tap
 * restart' descends 5 below its baseline of 53, so its ink runs to 58). 64
 * gives 12 px above the title's ink and 6 below the sub's. */
#define OVL_Y  140
#define OVL_W  184
#define OVL_H  64
#define OVL_T1 (OVL_Y + 27)	/* title baseline */
#define OVL_T2 (OVL_Y + 53)	/* sub baseline: one line step below */

#define COL_BG    ASTER_RGB565(250, 248, 239)
#define COL_BOARD ASTER_RGB565(187, 173, 160)
#define COL_HUD   ASTER_RGB565(119, 110, 101)
#define COL_MUTED ASTER_RGB565(160, 150, 140)
#define COL_PANEL ASTER_RGB565(238, 228, 218)
#define COL_WIN   ASTER_RGB565(237, 194, 46)
#define COL_OVER  ASTER_RGB565(246, 94, 59)

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[128];
static uint8_t arena[512];
static uint32_t W, H;
static uint32_t board[N * N];
static uint32_t score;
static uint32_t best;
static int dead;
static int keep_going;
static int repaint; /* board changed: the next FRAME draws */
static int drew;    /* first present logged */
static uint32_t rng;
static int32_t down_x = -1, down_y = -1;
static int32_t ox, oy;
static int audio_ready, music_on = 1, sfx_on = 1, music_failed;
enum { AUDIO_NONE, AUDIO_MUSIC, AUDIO_EFFECT };
static int audio_kind;
static int32_t effect_handles[4];

static const char *const music_path = "/assets/audio/bgm.mp3";
static const char *const slide_path = "/assets/audio/slide.wav";
static const char *const merge_path = "/assets/audio/merge.wav";
static const char *const win_path = "/assets/audio/win.wav";
static const char *const over_path = "/assets/audio/over.wav";
static const char *const effect_paths[4] = {
	"/assets/audio/slide.wav", "/assets/audio/merge.wav",
	"/assets/audio/win.wav", "/assets/audio/over.wav",
};

static void audio_load_effects(void)
{
	if (!audio_ready) {
		return;
	}
	for (int i = 0; i < 4; i++) {
		int32_t handle = aster_audio_effect_load(effect_paths[i],
			(uint32_t)strlen(effect_paths[i]));
		effect_handles[i] = handle > 0 ? handle : 0;
	}
}

static void audio_start_music(void)
{
	if (!audio_ready || !music_on || music_failed) {
		return;
	}
	if (aster_audio_start_file(music_path, (uint32_t)strlen(music_path)) != ASTER_OK) {
		music_failed = 1;
		audio_kind = AUDIO_NONE;
		return;
	}
	audio_kind = AUDIO_MUSIC;
}

static void audio_effect(const char *path)
{
	if (!audio_ready || !sfx_on) {
		return;
	}
	for (int i = 0; i < 4; i++) {
		if (strcmp(path, effect_paths[i]) == 0 && effect_handles[i] > 0) {
			if (aster_audio_effect_play((uint32_t)effect_handles[i]) == ASTER_OK &&
			    audio_kind != AUDIO_MUSIC) {
				audio_kind = AUDIO_EFFECT;
			}
			return;
		}
	}
}

static void audio_tick(void)
{
	aster_audio_state_t st = { .size = sizeof(st), .version = 2 };

	if (!audio_ready || aster_audio_get_state(&st, sizeof(st)) != ASTER_OK) {
		return;
	}
	if (st.state == ASTER_AUDIO_IDLE) {
		audio_kind = AUDIO_NONE;
		audio_start_music(); /* loop BGM after the stream ends */
	}
}

static void audio_toggle_music(void)
{
	music_on = !music_on;
	if (music_on) {
		music_failed = 0;
	}
	(void)aster_kv_set("music", 5, &music_on, sizeof(music_on));
	repaint = 1;
	if (!audio_ready) {
		return;
	}
	if (!music_on) {
		if (audio_kind == AUDIO_MUSIC) {
			(void)aster_audio_stop();
			audio_kind = AUDIO_NONE;
		}
	} else if (audio_kind == AUDIO_EFFECT) {
		(void)aster_audio_stop();
		audio_kind = AUDIO_NONE;
		audio_start_music();
	} else if (audio_kind == AUDIO_NONE) {
		audio_start_music();
	}
}

static void audio_toggle_sfx(void)
{
	sfx_on = !sfx_on;
	(void)aster_kv_set("sfx", 3, &sfx_on, sizeof(sfx_on));
	repaint = 1;
}

static uint32_t rnd(void)
{
	rng = rng * 1664525u + 1013904223u;
	return rng;
}

static uint16_t tile_bg(uint32_t v)
{
	switch (v) {
	case 0:    return ASTER_RGB565(205, 193, 180);
	case 2:    return ASTER_RGB565(238, 228, 218);
	case 4:    return ASTER_RGB565(237, 224, 200);
	case 8:    return ASTER_RGB565(242, 177, 121);
	case 16:   return ASTER_RGB565(245, 149, 99);
	case 32:   return ASTER_RGB565(246, 124, 95);
	case 64:   return ASTER_RGB565(246, 94, 59);
	case 128:  return ASTER_RGB565(237, 207, 114);
	case 256:  return ASTER_RGB565(237, 204, 97);
	case 512:  return ASTER_RGB565(237, 200, 80);
	case 1024: return ASTER_RGB565(237, 197, 63);
	case 2048: return ASTER_RGB565(237, 194, 46);
	default:   return ASTER_RGB565(60, 58, 50);
	}
}

static uint16_t tile_fg(uint32_t v)
{
	return v <= 4u ? ASTER_RGB565(119, 110, 101) : ASTER_RGB565(249, 246, 242);
}

/* Centred run at `base`. Measures rather than assuming tabular advances:
 * the size snaps to a baked cpfont face whose advances are the font's. */
static void text_center(int32_t cx, int32_t base, uint32_t px, uint16_t fg,
			const char *s)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w < 0) {
		return;
	}
	aster_ge_text(&ge, cx - w / 2, base, px, fg, ASTER_GFX_NOBG, s);
}

static int empty_n(void)
{
	int i, n = 0;

	for (i = 0; i < N * N; i++) {
		if (!board[i]) {
			n++;
		}
	}
	return n;
}

static int has_won(void)
{
	int i;

	for (i = 0; i < N * N; i++) {
		if (board[i] >= 2048u) {
			return 1;
		}
	}
	return 0;
}

static int can_move(void)
{
	int r, c;

	if (empty_n()) {
		return 1;
	}
	for (r = 0; r < N; r++) {
		for (c = 0; c < N; c++) {
			uint32_t v = board[r * N + c];

			if (c + 1 < N && board[r * N + c + 1] == v) {
				return 1;
			}
			if (r + 1 < N && board[(r + 1) * N + c] == v) {
				return 1;
			}
		}
	}
	return 0;
}

static void spawn(void)
{
	int n = empty_n();
	int k, i;

	if (n <= 0) {
		return;
	}
	k = (int)(rnd() % (uint32_t)n);
	for (i = 0; i < N * N; i++) {
		if (board[i]) {
			continue;
		}
		if (k == 0) {
			board[i] = (rnd() % 10u) == 0u ? 4u : 2u;
			return;
		}
		k--;
	}
}

static void load_best(void)
{
	uint32_t v = 0;

	if (aster_kv_get("best", 4, &v, 4) == 4) {
		best = v;
	}
}

static void save_best(void)
{
	if (score > best) {
		best = score;
		aster_kv_set("best", 4, &best, 4);
	}
}

static void reset_game(void)
{
	int i;

	for (i = 0; i < N * N; i++) {
		board[i] = 0;
	}
	score = 0;
	dead = 0;
	keep_going = 0;
	repaint = 1;
	rng ^= aster_time_now_ms() | 1u;
	spawn();
	spawn();
}

/* Slide one line toward index 0. Returns 1 if anything changed. */
static int slide4(uint32_t a[N], uint32_t *add)
{
	uint32_t t[N] = { 0 };
	int n = 0, merged = 0, i, changed = 0;

	for (i = 0; i < N; i++) {
		if (!a[i]) {
			continue;
		}
		if (n && t[n - 1] == a[i] && !merged) {
			t[n - 1] *= 2u;
			*add += t[n - 1];
			merged = 1;
		} else {
			t[n++] = a[i];
			merged = 0;
		}
	}
	for (i = 0; i < N; i++) {
		if (a[i] != t[i]) {
			changed = 1;
		}
		a[i] = t[i];
	}
	return changed;
}

static int move(int dir)
{
	int i, j, changed = 0;
	int won_before = has_won();
	uint32_t add = 0;
	uint32_t line[N];

	for (i = 0; i < N; i++) {
		if (dir == 0) { /* left */
			for (j = 0; j < N; j++) {
				line[j] = board[i * N + j];
			}
		} else if (dir == 2) { /* right */
			for (j = 0; j < N; j++) {
				line[j] = board[i * N + (N - 1 - j)];
			}
		} else if (dir == 3) { /* up */
			for (j = 0; j < N; j++) {
				line[j] = board[j * N + i];
			}
		} else { /* down */
			for (j = 0; j < N; j++) {
				line[j] = board[(N - 1 - j) * N + i];
			}
		}
		if (slide4(line, &add)) {
			changed = 1;
		}
		if (dir == 0) {
			for (j = 0; j < N; j++) {
				board[i * N + j] = line[j];
			}
		} else if (dir == 2) {
			for (j = 0; j < N; j++) {
				board[i * N + (N - 1 - j)] = line[j];
			}
		} else if (dir == 3) {
			for (j = 0; j < N; j++) {
				board[j * N + i] = line[j];
			}
		} else {
			for (j = 0; j < N; j++) {
				board[(N - 1 - j) * N + i] = line[j];
			}
		}
	}
	if (changed) {
		score += add;
		save_best();
		spawn();
		repaint = 1;
		if (!can_move()) {
			dead = 1;
		}
		if (dead) {
			audio_effect(over_path);
		} else if (!won_before && has_won()) {
			audio_effect(win_path);
		} else {
			audio_effect(add ? merge_path : slide_path);
		}
	}
	return changed;
}

static void draw_hud(void)
{
	/* Baselines from the face's own ascent and line step rather than
	 * literals: 21 is as close to the top edge as a 20 px line can sit, and
	 * the second line is exactly one step below it. */
	const int32_t b1 = ASTER_TEXT_FACES[0].asc;
	char line[28];
	int32_t w;

	aster_snprintf(line, (int)sizeof(line), "SCORE %u", (unsigned)score);
	aster_ge_text(&ge, 8, b1, NUM_PX, COL_HUD, ASTER_GFX_NOBG, line);
	aster_snprintf(line, (int)sizeof(line), "BEST %u", (unsigned)best);
	aster_ge_text(&ge, 8, b1 + ASTER_TEXT_FACES[0].line_h, NUM_PX, COL_MUTED,
		    ASTER_GFX_NOBG, line);
	/* Two independent controls in the right half of the HUD. */
	const char *music = music_on ? "BGM ON" : "BGM OFF";
	w = aster_gfx_text_w(NUM_PX, music, (uint32_t)strlen(music));
	if (w > 0) {
		aster_ge_text(&ge, (int32_t)W - 8 - w, b1, NUM_PX,
			    audio_ready ? COL_HUD : COL_MUTED, ASTER_GFX_NOBG, music);
	}
	const char *sfx = sfx_on ? "SFX ON" : "SFX OFF";
	w = aster_gfx_text_w(NUM_PX, sfx, (uint32_t)strlen(sfx));
	if (w > 0) {
		aster_ge_text(&ge, (int32_t)W - 8 - w,
			    b1 + ASTER_TEXT_FACES[0].line_h, NUM_PX, COL_MUTED,
			    ASTER_GFX_NOBG, sfx);
	}
}

static void draw_tile(int r, int c)
{
	uint32_t v = board[r * N + c];
	int32_t x = ox + c * (CELL + GAP);
	int32_t y = oy + r * (CELL + GAP);
	char num[8];
	int32_t tw;

	aster_ge_rect_r(&ge, x, y, CELL, CELL, tile_bg(v), TILE_R, 0);
	if (!v) {
		return;
	}
	aster_snprintf(num, (int)sizeof(num), "%u", (unsigned)v);
	tw = aster_gfx_text_w(NUM_PX, num, (uint32_t)strlen(num));
	if (tw < 0) {
		return;
	}
	aster_ge_text(&ge, x + ((int32_t)CELL - tw) / 2, y + NUM_BASE, NUM_PX,
		    tile_fg(v), ASTER_GFX_NOBG, num);
}

static void draw_overlay(const char *title, uint16_t border, const char *sub)
{
	int32_t x = (int32_t)(W - OVL_W) / 2;
	int32_t cx = x + OVL_W / 2;

	aster_ge_rect_r(&ge, x, OVL_Y, OVL_W, OVL_H, COL_PANEL, 8, 0);
	aster_ge_rect_r(&ge, x, OVL_Y, OVL_W, OVL_H, border, 8, 1);
	/* OVL_T1 / OVL_T2 come from the geometry note at the top: the title is
	 * one ascent-and-half-margin down, the sub exactly one line step below
	 * it. Two 20 px lines need a 64 px panel; at the old 56 they overlapped. */
	text_center(cx, OVL_T1, NUM_PX, border, title);
	text_center(cx, OVL_T2, NUM_PX, COL_HUD, sub);
}

static int32_t draw(void)
{
	int r, c;
	uint32_t bw = (uint32_t)N * CELL + (uint32_t)(N + 1) * GAP;

	/* Full repaint first: a panel canvas alternates front buffers, so
	 * anything this frame does not touch still holds the frame from two
	 * presents ago. aster_ge_clear covers the whole surface, which is also
	 * what makes the accumulated dirty rect moot. */
	aster_ge_clear(&ge, COL_BG);
	aster_ge_rect(&ge, ox - GAP, oy - GAP, bw, bw, COL_BOARD);
	draw_hud();
	for (r = 0; r < N; r++) {
		for (c = 0; c < N; c++) {
			draw_tile(r, c);
		}
	}
	if (dead) {
		draw_overlay("GAME OVER", COL_OVER, "tap restart");
	} else if (!keep_going && has_won()) {
		draw_overlay("YOU WIN", COL_WIN, "tap continue");
	}
	return aster_ge_present(&ge);
}

static void on_touch(const aster_event_t *ev)
{
	int32_t x = (int32_t)ev->u.touch.x;
	int32_t y = (int32_t)ev->u.touch.y;
	int32_t dx, dy, adx, ady;

	if (ev->u.touch.action == ASTER_TOUCH_DOWN) {
		down_x = x;
		down_y = y;
		return;
	}
	if (ev->u.touch.action != ASTER_TOUCH_UP || down_x < 0) {
		return;
	}
	dx = x - down_x;
	dy = y - down_y;
	if (down_x >= (int32_t)W - 92 && x >= (int32_t)W - 92 &&
	    down_y >= 0 && down_y < HUD && y >= 0 && y < HUD &&
	    dx > -16 && dx < 16 && dy > -16 && dy < 16) {
		if (y < 30) {
			audio_toggle_music();
		} else {
			audio_toggle_sfx();
		}
		down_x = down_y = -1;
		return;
	}
	down_x = down_y = -1;
	if (dead) {
		reset_game();
		audio_effect(slide_path);
		return;
	}
	if (has_won() && !keep_going) {
		keep_going = 1;
		repaint = 1; /* the YOU WIN panel goes away */
		return;
	}
	adx = dx < 0 ? -dx : dx;
	ady = dy < 0 ? -dy : dy;
	if (adx < 16 && ady < 16) {
		return;
	}
	if (adx >= ady) {
		move(dx > 0 ? 2 : 0);
	} else {
		move(dy > 0 ? 1 : 3);
	}
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = {
		.size = sizeof(d), .version = 1, .format = ASTER_PIXEL_NATIVE,
		/* Panel canvas: this app repaints the whole board every frame
		 * (aster_ge_clear + scene), which is exactly the contract the
		 * double-buffered canvas needs -- and it drops the ~7 ms
		 * byte-order sweep out of every present. The firmware may
		 * refuse (rotated panel, buffers busy); buf.flags below says
		 * which one we got, and the engine path is identical either
		 * way, so nothing else here depends on the answer. */
		.flags = ASTER_SURFACE_F_PANEL_CANVAS,
	};
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	int32_t err;

	aster_app_set_framerate(20);
	err = aster_surface_create(&d, sizeof(d), &surf);
	if (err != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "2048 surface: %d", err);
		return err;
	}
	err = aster_surface_acquire(surf, &buf, sizeof(buf));
	if (err != ASTER_OK) {
		return err;
	}
	W = buf.width;
	H = buf.height;
	aster_ge_init(&ge, surf, W, H, ring, sizeof(ring) / sizeof(ring[0]),
		    arena, sizeof(arena));
	ox = (int32_t)(W - ((uint32_t)N * CELL + (uint32_t)(N - 1) * GAP)) / 2;
	oy = HUD;
	rng = aster_time_now_ms() ^ 0x20482048u;
	load_best();
	{
		int value;

		if (aster_kv_get("music", 5, &value, sizeof(value)) == sizeof(value)) {
			music_on = value != 0;
		}
		if (aster_kv_get("sfx", 3, &value, sizeof(value)) == sizeof(value)) {
			sfx_on = value != 0;
		}
	}
	audio_ready = aster_system_has_feature(ASTER_FEATURE_AUDIO_OUTPUT) > 0;
	music_failed = 0;
	audio_kind = AUDIO_NONE;
	reset_game();
	audio_load_effects();
	audio_start_music();
	aster_logf(ASTER_LOG_INFO, "2048 %ux%u best=%u canvas=%d", W, H,
		 (unsigned)best, (buf.flags & ASTER_SURFACE_F_PANEL_CANVAS) != 0u);
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	save_best();
	if (audio_ready) {
		(void)aster_audio_stop();
	}
	for (int i = 0; i < 4; i++) {
		if (effect_handles[i] > 0) {
			(void)aster_audio_effect_unload((uint32_t)effect_handles[i]);
			effect_handles[i] = 0;
		}
	}
	audio_kind = AUDIO_NONE;
	if (surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(surf);
		surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_pause(void)
{
	if (audio_ready && audio_kind != AUDIO_NONE) {
		(void)aster_audio_pause();
	}
}

ASTER_APP_EXPORT void aster_app_resume(void)
{
	if (audio_ready && audio_kind != AUDIO_NONE) {
		(void)aster_audio_resume();
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	switch (ev->type) {
	case ASTER_EVENT_TOUCH:
		on_touch(ev);
		break;
	case ASTER_EVENT_FRAME:
		audio_tick();
		/* 2048 is turn-based: the board only changes on touch, so an
		 * idle frame has nothing to say. Skipping the whole draw (not
		 * just the present) keeps a still board from paying 17 ms of
		 * full-screen raster 20 times a second -- and skipping the
		 * present too is safe: the glass keeps the buffer it already
		 * has, and every present we DO make is a full repaint, which
		 * is the canvas contract. */
		if (repaint) {
			int32_t ran = draw();

			repaint = 0;
			if (!drew) {
				/* A canvas app's failure mode is a blank screen
				 * with no error anywhere: this one line says
				 * whether the frame reached the engine at all
				 * (positive = commands rasterized, negative =
				 * aster_error_t). Also confirms the panel canvas
				 * is the same full-screen shape the board math
				 * assumes. */
				drew = 1;
				aster_logf(ASTER_LOG_INFO,
					 "2048 first frame: %d cmds, %ux%u", ran,
					 W, H);
			}
		}
		break;
	default:
		break;
	}
}
