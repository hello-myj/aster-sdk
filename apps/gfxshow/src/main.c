/* SPDX-License-Identifier: MIT
 * gfxshow — ASTER gfx engine (ABI 2.2) feature showcase + perf probe.
 *
 * Drives every command the engine understands (clear/gradient/rect/rounded/
 * ellipse/AA-line/triangle/text/sprite) from one 30 fps immediate-mode
 * render loop, and prints measured frame rate + command count over the
 * serial log every 60 frames so `verify` can capture it.
 *
 * If a picture exists under /sd it is loaded once as a sprite and blitted
 * (scaled + alpha) — otherwise the demo runs pure vector. Tap to toggle the
 * sprite spin; the loop never blocks on decode.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster_text.h>

/* The one baked face, and the title's baseline: `asc` rows down from the top
 * edge of the 26 px band the title has always sat in. */
#define PX         ((int)ASTER_TEXT_FACES[0].px)
#define TITLE_BASE (26 + ASTER_TEXT_FACES[0].asc)

/* The panel is pinned to the bottom edge, so everything else here is already
 * relative to the glass: 118 is 110 of panel plus an 8 px bottom margin, and
 * the ellipse and HUD baselines below measure down from H for the same
 * reason. */
#define PANEL_M    8
#define PANEL_H    110
#define PANEL_Y    ((int32_t)H - PANEL_M - PANEL_H)

static aster_handle_t s_surf = ASTER_HANDLE_INVALID;
static aster_ge_t     s_ge;
static uint32_t     W, H;

static aster_gfx_cmd_t s_ring[160];
static uint8_t       s_arena[1024];

/* sprite (optional) */
static uint32_t s_bmp;	/* handle, 0 = none */
static uint32_t s_bw, s_bh;
static int      s_spin = 1;

/* fps probe */
static uint32_t s_frames;
static uint32_t s_t0;
/* encoding vs post-exec split (ms per frame over the reporting window).
 * `enc` = command encoding; `post` = raster + flush (inseparable here). */
static uint32_t s_t_enc;
static uint32_t s_t_post;
static uint32_t s_post_max;

/* cheap deterministic pseudo-poly (no libm in the app) */
static int32_t isin(int32_t a)	/* a in 0..255 ~ 0..360deg, result *256 */
{
	/* triangle-wave approximation scaled to a sine-ish curve */
	int32_t t = a & 255;
	int32_t v;

	if (t < 64) {
		v = t * 4;
	} else if (t < 192) {
		v = 256 - (t - 64) * 4;
	} else {
		v = (t - 192) * 4 - 256;
	}
	return v * 256 / 256;
}
static int32_t icos(int32_t a)
{
	return isin(a + 64);
}

static uint16_t hsv565(uint32_t h)
{
	uint32_t i = (h / 43u) % 6u;
	uint32_t f = (h / 43u) % 43u;
	uint32_t p = (240u * (43u - f) / 43u);
	uint32_t q = (240u - 240u * f / 43u);
	uint32_t r, g, b;

	switch (i) {
	case 0: r = 240; g = q; b = p; break;
	case 1: r = p; g = 240; b = q; break;
	case 2: r = q; g = p; b = 240; break;
	case 3: r = 240; g = p; b = p; break;
	case 4: r = q; g = 240; b = p; break;
	default: r = p; g = q; b = 240; break;
	}
	return ASTER_RGB565(r, g, b);
}

static const char *EXT(const char *s)
{
	const char *d = s;

	for (const char *p = s; *p; p++) {
		if (*p == '.') {
			d = p;
		}
	}
	return d;
}

static void try_load_sprite(void)
{
	aster_handle_t dh;

	if (aster_fs_opendir("/sd", 3, &dh) != ASTER_OK) {
		return;
	}
	char name[48];
	uint32_t isdir;

	for (;;) {
		int32_t n = aster_fs_dir_next(dh, name, sizeof(name), &isdir);

		if (n <= 0) {
			break;
		}
		if (isdir) {
			continue;
		}
		const char *e = EXT(name);

		if (strcmp(e, ".bmp") && strcmp(e, ".png") && strcmp(e, ".jpg")) {
			continue;
		}
		char path[96];

		aster_snprintf(path, sizeof(path), "/sd/%s", name);
		uint32_t ow = 0, oh = 0;
		int32_t r = aster_gfx_bmp_load(path, (uint32_t)strlen(path), 96, 96,
					     &s_bmp, &ow, &oh);

		if (r == ASTER_OK && s_bmp) {
			s_bw = ow;
			s_bh = oh;
			aster_logf(ASTER_LOG_INFO, "gfxshow sprite %s %ux%u h=%u", name,
				 ow, oh, s_bmp);
			break;
		}
	}
	aster_fs_closedir(dh);
}

static void draw(void)
{
	uint32_t t = s_frames;
	uint32_t t0 = aster_time_now_ms();

	aster_ge_clear(&s_ge, ASTER_RGB565(8, 12, 22));
	/* background diagonal gradient band */
	aster_ge_grad(&s_ge, 0, 0, W, H / 2, ASTER_RGB565(20, 30, 70),
		    ASTER_RGB565(70, 24, 90), 1);

	/* rounded panel */
	aster_ge_rect_r(&s_ge, PANEL_M, PANEL_Y, W - 2 * PANEL_M, PANEL_H,
		      ASTER_RGB565(28, 34, 52), 10, 0);
	aster_ge_rect_r(&s_ge, PANEL_M, PANEL_Y, W - 2 * PANEL_M, PANEL_H,
		      ASTER_RGB565(120, 130, 160), 10, 1);

	/* rotating triangle */
	{
		int32_t a = (int32_t)(t * 3) & 255;
		int32_t cx = W / 2, cy = 92, rr = 58;
		int32_t x0 = cx + icos(a) * rr / 256, y0 = cy + isin(a) * rr / 256;
		int32_t x1 = cx + icos(a + 85) * rr / 256, y1 = cy + isin(a + 85) * rr / 256;
		int32_t x2 = cx + icos(a + 170) * rr / 256, y2 = cy + isin(a + 170) * rr / 256;

		aster_ge_tri(&s_ge, x0, y0, x1, y1, x2, y2, hsv565(t * 5u));
	}

	/* pulsing ellipse (fill) + its outline (stroke) */
	{
		uint32_t rx = 40 + (uint32_t)(isin((int32_t)(t * 2) & 255) + 256) / 24;

		aster_ge_ellipse(&s_ge, 58, H - 64, rx, rx / 2, ASTER_RGB565(52, 211, 153), 0);
		aster_ge_ellipse(&s_ge, W - 58, H - 64, 30, 30, ASTER_RGB565(244, 63, 94), 1);
	}

	/* AA crosshair lines sweeping across the top */
	for (int k = 0; k < 6; k++) {
		uint32_t ph = (t * 2u + k * 40u) & 255u;
		int32_t x0 = (int32_t)(k * (W / 6));
		int32_t y0 = 8 + isin((int32_t)ph) * 20 / 256 + 20;
		int32_t x1 = x0 + W / 6;
		int32_t y1 = 8 + isin((int32_t)((ph + 40) & 255)) * 20 / 256 + 20;

		aster_ge_line_aa(&s_ge, x0, y0, x1, y1, hsv565(k * 40u + t));
	}

	/* sprite: spin + alpha pulse (if one loaded) */
	if (s_bmp) {
		int32_t a = (int32_t)(t * 2) & 255;
		uint32_t scale = s_spin ? (256u + (uint32_t)icos(a) / 4u) : 256u;
		uint32_t alpha = s_spin ? (160u + (uint32_t)(isin(a) + 256) / 4u) : 255u;

		if (alpha > 255u) {
			alpha = 255u;
		}
		aster_ge_bmp(&s_ge, s_bmp, s_bw, s_bh, (int32_t)(W - s_bw * scale / 256u) / 2,
			   (int32_t)(H / 2 - s_bh * scale / 256u) / 2 + 40, (uint16_t)scale,
			   (uint16_t)alpha);
	}

	/* text: title measured + centered, then CJK/Latin HUD in the panel.
	 *
	 * Both runs name the one baked face. They used to name 20 and 16, which
	 * is also what the ABI snapped both of them to -- the numbers were
	 * already the same size, so nothing moves; they are written as the face
	 * now so a re-bake carries them instead of leaving two stale literals
	 * that would quietly disagree with the layout. */
	{
		const char *title = "ASTER GFX ENGINE";
		int32_t tw = aster_gfx_text_w(PX, title, (uint32_t)strlen(title));

		if (tw > 0) {
			aster_ge_text(&s_ge, (int32_t)((W - tw) / 2), TITLE_BASE, PX,
				    ASTER_RGB565(235, 238, 245), ASTER_GFX_NOBG, title);
		}
		/* 199.44 px wide at this face, from x=16 -- the panel's inner edge
		 * (W-16 = 232) still clears it by 16. */
		aster_ge_text(&s_ge, 16, H - 96, PX, ASTER_RGB565(150, 200, 255),
			    ASTER_GFX_NOBG, "图形引擎演示 混排 OK");
	}

	/* Frame budget, cut where the app can cut it: `enc` is command
	 * encoding only (a few stores per op), `post` is everything inside
	 * aster_gfx_exec() -- rasterization AND the panel flush, which the app
	 * cannot separate because both happen inside that one call.
	 * Do NOT read `post` as the flush cost: it is the sum. For the real
	 * raster-vs-flush split use CONFIG_ASTER_GFX_TIMING, which cuts inside
	 * the native call and prints per-op attribution too. */
	s_t_enc += aster_time_now_ms() - t0;
	t0 = aster_time_now_ms();
	aster_ge_present(&s_ge);
	{
		uint32_t d = aster_time_now_ms() - t0;

		s_t_post += d;
		if (d > s_post_max) {
			s_post_max = d;
		}
	}
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = { .size = sizeof(d), .version = 1,
				 .format = ASTER_PIXEL_RGB565,
				 .flags = ASTER_SURFACE_F_PANEL_CANVAS };
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	int32_t err;

	/* This demo is the engine's performance probe, so it asks for a cap
	 * well above what the pipeline can reach: the reported fps is then
	 * the real work-bound figure instead of the requested rate. */
	aster_app_set_framerate(60);
	err = aster_surface_create(&d, sizeof(d), &s_surf);
	if (err != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "gfxshow surface: %d", err);
		return err;
	}
	err = aster_surface_acquire(s_surf, &buf, sizeof(buf));
	if (err != ASTER_OK) {
		return err;
	}
	W = buf.width;
	H = buf.height;
	aster_ge_init(&s_ge, s_surf, W, H, s_ring,
		    sizeof(s_ring) / sizeof(s_ring[0]), s_arena, sizeof(s_arena));
	try_load_sprite();
	s_t0 = aster_time_now_ms();
	aster_logf(ASTER_LOG_INFO, "gfxshow %ux%u engine up%s", W, H,
		 (buf.flags & ASTER_SURFACE_F_PANEL_CANVAS) ? " (panel canvas)" : "");
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	if (s_bmp) {
		aster_gfx_bmp_free(s_bmp);
		s_bmp = 0;
	}
	if (s_surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(s_surf);
		s_surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	switch (ev->type) {
	case ASTER_EVENT_TOUCH:
		if (ev->u.touch.action == ASTER_TOUCH_DOWN) {
			s_spin = !s_spin;
		}
		break;
	case ASTER_EVENT_FRAME:
		draw();
		s_frames++;
		if ((s_frames % 60u) == 0u) {
			uint32_t dt = aster_time_now_ms() - s_t0;
			uint32_t fps = dt ? (60000u / dt) : 0;

			aster_logf(ASTER_LOG_INFO,
				 "gfxshow fps=%u t=%u frames=%u enc=%u post=%u "
				 "post_max=%u",
				 (unsigned)fps, (unsigned)dt, (unsigned)s_frames,
				 (unsigned)(s_t_enc / 60u),
				 (unsigned)(s_t_post / 60u), (unsigned)s_post_max);
			s_t0 = aster_time_now_ms();
			s_t_enc = 0;
			s_t_post = 0;
			s_post_max = 0;
		}
		break;
	default:
		break;
	}
}
