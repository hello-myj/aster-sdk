/* SPDX-License-Identifier: MIT
 * snake — swipe to steer, tap after death to restart.
 *
 * Uses the surface + gfx path (same as the old paint canvas): native UI
 * has no set_color, so a grid game draws RGB565 itself.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster_text.h>

#define CELL 16
#define HUD  32

/* Upper bound on the grid, for the body arrays. 20 x 20 is the square case
 * of a 320 px edge at 16 px a cell, which bounds both of this board's
 * orientations -- 15 x 18 at 240x320 and 20 x 13 at 320x240 -- with room to
 * spare. */
#define COLS_MAX 20
#define ROWS_MAX 20
#define MAX      (COLS_MAX * ROWS_MAX)

/* The grid is resolved at init: cols x CELL fills the panel's width exactly
 * and rows x CELL fills what is left under the HUD, so it always ends flush
 * against both edges. These were the literals 15 and 18, which is the
 * portrait answer -- and on a 240-tall screen it is a grid 288 px tall that
 * runs off the bottom edge with no assert, no log and no clip rectangle to
 * stop it. HUD is not a free parameter the way it is in 2048, because the
 * grid is measured from it; it only has to clear the HUD's own ink, and one
 * 20 px line at baseline 21 puts that on rows 6..22 ('SCORE n' is uppercase
 * and digits, so nothing descends), which clears the grid by 9. */
static int cols, rows;

#define HUD_BASE ((int32_t)ASTER_TEXT_FACES[0].asc)

/* GAME OVER panel. Two 20 px lines: the title's ink is 15 rows above its
 * baseline and stops there, the sub's runs 5 below. Baselines 20 and 46 into
 * the panel put that block at rows 5..51 of its 56 -- 5 px clear at each end. */
#define OVL_Y  140
#define OVL_W  184
#define OVL_H  56
#define OVL_T1 (OVL_Y + 20)	/* title baseline */
#define OVL_T2 (OVL_Y + 46)	/* sub baseline */

/* The one baked face. Every size argument below used to name a different
 * number (12 for the HUD, 14 for the title, 12 for the sub); they all draw at
 * this one now, so they are one constant. */
#define PX ((int)ASTER_TEXT_FACES[0].px)

#define COL_BG    ASTER_RGB565(10, 14, 26)
#define COL_GRID  ASTER_RGB565(24, 32, 48)
#define COL_SNAKE ASTER_RGB565(16, 185, 129)
#define COL_HEAD  ASTER_RGB565(52, 211, 153)
#define COL_FOOD  ASTER_RGB565(244, 63, 94)
#define COL_HUD   ASTER_RGB565(230, 235, 245)
#define COL_MUTED ASTER_RGB565(90, 100, 120)
#define COL_OVER  ASTER_RGB565(248, 113, 113)

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[512];
static uint8_t arena[512];
static uint32_t W, H;

/* Centred run at `base`. Measures rather than assuming the advances. */
static void text_center(int32_t cx, int32_t base, uint16_t fg, const char *s)
{
	int32_t w = aster_gfx_text_w(PX, s, (uint32_t)strlen(s));

	if (w < 0) {
		return;
	}
	aster_ge_text(&ge, cx - w / 2, base, PX, fg, ASTER_GFX_NOBG, s);
}

static uint8_t bx[MAX];
static uint8_t by[MAX];
static int len;
static int dir; /* 0 right, 1 down, 2 left, 3 up */
static int pending;
static int food_x, food_y;
static int dead;
static int score;
static uint32_t rng;
static int32_t down_x = -1, down_y = -1;

static const int8_t DX[4] = { 1, 0, -1, 0 };
static const int8_t DY[4] = { 0, 1, 0, -1 };

static uint32_t rnd(void)
{
	rng = rng * 1664525u + 1013904223u;
	return rng;
}

static int occupied(int x, int y, int from)
{
	int i;

	for (i = from; i < len; i++) {
		if (bx[i] == (uint8_t)x && by[i] == (uint8_t)y) {
			return 1;
		}
	}
	return 0;
}

static void place_food(void)
{
	int n = cols * rows - len;
	int k, x, y;

	if (n <= 0) {
		food_x = food_y = -1;
		return;
	}
	k = (int)(rnd() % (uint32_t)n);
	for (y = 0; y < rows; y++) {
		for (x = 0; x < cols; x++) {
			if (occupied(x, y, 0)) {
				continue;
			}
			if (k == 0) {
				food_x = x;
				food_y = y;
				return;
			}
			k--;
		}
	}
	food_x = food_y = -1;
}

static void reset_game(void)
{
	/* Middle of the grid, heading left. Written as the grid's own centre
	 * rather than the literals (7, 9): those are 15/2 and 18/2, so portrait
	 * lands on exactly the same cells, and a landscape grid starts where the
	 * eye already is instead of a third of the way along the top. */
	len = 3;
	bx[0] = (uint8_t)(cols / 2);
	by[0] = (uint8_t)(rows / 2);
	bx[1] = (uint8_t)(cols / 2 - 1);
	by[1] = (uint8_t)(rows / 2);
	bx[2] = (uint8_t)(cols / 2 - 2);
	by[2] = (uint8_t)(rows / 2);
	dir = 0;
	pending = 0;
	dead = 0;
	score = 0;
	rng ^= aster_time_now_ms() | 1u;
	place_food();
}

static void draw(void)
{
	int i, x, y;
	int32_t tw;
	char line[24];
	uint32_t ox, oy;

	ox = (W - (uint32_t)cols * CELL) / 2u;
	oy = HUD;
	/* ABI 2.2 engine path: one exec per frame, firmware rasterizes */
	aster_ge_clear(&ge, COL_BG);

	for (y = 0; y <= rows; y++) {
		aster_ge_rect(&ge, (int32_t)ox, (int32_t)(oy + y * CELL),
			    cols * CELL, 1, COL_GRID);
	}
	for (x = 0; x <= cols; x++) {
		aster_ge_rect(&ge, (int32_t)(ox + x * CELL), (int32_t)oy, 1,
			    rows * CELL, COL_GRID);
	}

	if (food_x >= 0) {
		aster_ge_ellipse(&ge, (int32_t)(ox + food_x * CELL + CELL / 2),
			       (int32_t)(oy + food_y * CELL + CELL / 2), 5, 5,
			       COL_FOOD, 0);
	}
	for (i = len - 1; i >= 0; i--) {
		aster_ge_rect_r(&ge, (int32_t)(ox + bx[i] * CELL + 2),
			      (int32_t)(oy + by[i] * CELL + 2), CELL - 4, CELL - 4,
			      i == 0 ? COL_HEAD : COL_SNAKE, 4, 0);
	}

	aster_snprintf(line, (int)sizeof(line), "SCORE %d", score);
	aster_ge_text(&ge, 8, HUD_BASE, PX, COL_HUD, ASTER_GFX_NOBG, line);
	/* Right-aligned: "SCORE 12345" is 138 px at this face, so at its old
	 * x=140 the hint would have landed on top of the score. */
	tw = aster_gfx_text_w(PX, "swipe", 5u);
	if (tw > 0) {
		aster_ge_text(&ge, (int32_t)W - 8 - tw, HUD_BASE, PX, COL_MUTED,
			    ASTER_GFX_NOBG, "swipe");
	}

	if (dead) {
		int32_t px = (int32_t)(W - OVL_W) / 2;
		int32_t cx = px + OVL_W / 2;

		aster_ge_rect_r(&ge, px, OVL_Y, OVL_W, OVL_H, ASTER_RGB565(15, 18, 32),
			      8, 0);
		aster_ge_rect_r(&ge, px, OVL_Y, OVL_W, OVL_H, COL_OVER, 8, 1);
		/* Centred by measurement, not by the literals 72/70 those two
		 * used to be: 'GAME OVER' is 120.50 px wide and 'tap restart'
		 * 105.00, so one x cannot serve both. */
		text_center(cx, OVL_T1, COL_OVER, "GAME OVER");
		text_center(cx, OVL_T2, COL_HUD, "tap restart");
	}
	aster_ge_present(&ge);
}

static void step(void)
{
	int nx, ny, i;

	if (dead) {
		return;
	}
	dir = pending;
	nx = (int)bx[0] + DX[dir];
	ny = (int)by[0] + DY[dir];
	if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) {
		dead = 1;
		return;
	}
	if (nx == food_x && ny == food_y) {
		if (occupied(nx, ny, 0)) {
			dead = 1;
			return;
		}
		if (len < MAX) {
			len++;
		}
		score++;
		for (i = len - 1; i > 0; i--) {
			bx[i] = bx[i - 1];
			by[i] = by[i - 1];
		}
		bx[0] = (uint8_t)nx;
		by[0] = (uint8_t)ny;
		place_food();
		if (food_x < 0) {
			dead = 1;
		}
		return;
	}
	if (occupied(nx, ny, 0) && !(bx[len - 1] == (uint8_t)nx &&
				     by[len - 1] == (uint8_t)ny)) {
		dead = 1;
		return;
	}
	for (i = len - 1; i > 0; i--) {
		bx[i] = bx[i - 1];
		by[i] = by[i - 1];
	}
	bx[0] = (uint8_t)nx;
	by[0] = (uint8_t)ny;
}

static void steer(int nd)
{
	if (dead) {
		return;
	}
	if (((nd + 2) & 3) == dir) {
		return;
	}
	pending = nd;
}

static void on_touch(const aster_event_t *ev)
{
	int32_t x = (int32_t)ev->u.touch.x;
	int32_t y = (int32_t)ev->u.touch.y;
	int32_t dx, dy;

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
	down_x = down_y = -1;
	if (dead) {
		reset_game();
		return;
	}
	if (dx > 12 && dx >= (dy < 0 ? -dy : dy)) {
		steer(0);
	} else if (dx < -12 && -dx >= (dy < 0 ? -dy : dy)) {
		steer(2);
	} else if (dy > 12) {
		steer(1);
	} else if (dy < -12) {
		steer(3);
	}
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = {
		.size = sizeof(d), .version = 1, .format = ASTER_PIXEL_NATIVE,
		/* Panel canvas: this app repaints the whole board every frame
		 * (aster_ge_clear + scene), which is exactly the contract the
		 * double-buffered canvas needs -- and it drops the ~7 ms
		 * byte-order sweep out of every present. */
		.flags = ASTER_SURFACE_F_PANEL_CANVAS,
	};
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	int32_t err;

	aster_app_set_framerate(8);
	err = aster_surface_create(&d, sizeof(d), &surf);
	if (err != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "snake surface: %d", err);
		return err;
	}
	err = aster_surface_acquire(surf, &buf, sizeof(buf));
	if (err != ASTER_OK) {
		return err;
	}
	W = buf.width;
	H = buf.height;
	cols = (int)(W / CELL);
	rows = (int)((H - HUD) / CELL);
	if (cols < 4 || rows < 4) {
		/* No assertion to make here -- this is a runtime surface -- but a
		 * grid this small is a game nobody can play, and silently running
		 * one is worse than saying so in the log. */
		aster_logf(ASTER_LOG_ERROR, "snake: %ux%u leaves a %dx%d grid",
			 W, H, cols, rows);
		return ASTER_EINVAL;
	}
	aster_ge_init(&ge, surf, W, H, ring, sizeof(ring) / sizeof(ring[0]), arena,
		    sizeof(arena));
	rng = aster_time_now_ms() ^ 0xA5A5A5A5u;
	reset_game();
	aster_logf(ASTER_LOG_INFO, "snake %ux%u grid %dx%d", W, H, cols, rows);
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	if (surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(surf);
		surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	switch (ev->type) {
	case ASTER_EVENT_TOUCH:
		on_touch(ev);
		break;
	case ASTER_EVENT_FRAME:
		step();
		draw(); /* draw() ends in aster_ge_present() */
		break;
	default:
		break;
	}
}
