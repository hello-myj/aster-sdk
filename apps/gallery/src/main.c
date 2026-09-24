/* SPDX-License-Identifier: MIT
 * aster.gallery — picture viewer for the SD card (ABI 1.14).
 *
 * Scans /sd/picture (fallback /sd) for BMP/JPG/PNG and pages through them
 * with firmware-streamed decode: aster_img_render fills the surface back
 * buffer (used as scratch — pitch == out_w), the raster is then moved to
 * its centred rect and the whole surface is handed over in ONE
 * aster_surface_present() per redraw. Nothing about file or raster size
 * touches app memory; a 4K/72 MB source costs the same 150 KB surface.
 *
 * One full present per redraw, not a set of partial aster_surface_flush()
 * calls: a partial write invalidates the firmware's record of what is on
 * the glass, and that record is what `aster ui screenshot` reads back — a
 * flush-painted frame leaves the glass looking blank. Presenting the whole
 * surface also means every redraw repaints every pixel, which is what
 * erases the edges of a previously shown larger image behind a smaller
 * successor.
 *
 * Interaction: swipe L/R = prev/next, tap = next, x (top-right) = quit.
 * Decodes block the app worker (large PNG ~5 s), so touches only mark the
 * view dirty; the actual render runs on the frame tick and mid-decode
 * swipes collapse into a single page flip.
 */
#include <aster_imports.h>
#include <aster_gfx.h>

#define GAL_MAX    64
#define GAL_PATH   96
#define HUD_H      16
#define SWIPE_PX   40u
#define EXIT_W     48u
#define EXIT_H     40u

static aster_handle_t s_surf = ASTER_HANDLE_INVALID;
static uint16_t    *s_scr;        /* surface back buffer (app memory) */
static uint32_t     W, H, PITCH;

static char     s_names[GAL_MAX][GAL_PATH];
static int      s_cnt;
static int      s_idx;
static int      s_dirty;      /* request a render on the next frame tick */
static int32_t  s_down_x, s_down_y;
static int      s_down_valid;

/* ---------------- dir scan ---------------- */

static const char *last_dot(const char *s)
{
	const char *best = NULL;
	for (const char *p = strchr(s, '.'); p; p = strchr(p + 1, '.')) {
		best = p;
	}
	return best;
}

static int ext_is_image(const char *name)
{
	const char *dot = last_dot(name);
	if (!dot++) {
		return 0;
	}
	char e[5];
	int i;
	for (i = 0; i < 4 && dot[i]; i++) {
		e[i] = (char)((dot[i] >= 'A' && dot[i] <= 'Z') ? dot[i] + 32 : dot[i]);
	}
	e[i] = 0;
	if (i > 4) {
		return 0; /* extension longer than 4: no match possible */
	}
	return !strcmp(e, "bmp") || !strcmp(e, "png") ||
	       !strcmp(e, "jpg") || !strcmp(e, "jpeg");
}

static int scan_dir(const char *dir)
{
	aster_handle_t dh = ASTER_HANDLE_INVALID;
	char name[GAL_PATH - 8];
	uint32_t is_dir;
	int cnt = 0;

	if (aster_fs_opendir(dir, (uint32_t)strlen(dir), &dh) != ASTER_OK) {
		return -1;
	}
	for (;;) {
		int32_t n = aster_fs_dir_next(dh, name, sizeof(name), &is_dir);
		if (n <= 0) {
			break;
		}
		if (is_dir || name[0] == '.' || !ext_is_image(name) || cnt >= GAL_MAX) {
			continue;
		}
		aster_snprintf(s_names[cnt], GAL_PATH, "%s/%s", dir, name);
		cnt++;
	}
	aster_fs_closedir(dh);
	s_cnt = cnt;
	return cnt;
}

static void scan_all(void)
{
	if (scan_dir("/sd/picture") <= 0) {
		if (scan_dir("/sd") <= 0) {
			s_cnt = 0;
		}
	}
	s_idx = 0;
}

/* ---------------- HUD ---------------- */

static const char *fmt_name(aster_img_info_t *i)
{
	switch (i->fmt) {
	case ASTER_IMG_FMT_BMP: return "BMP";
	case ASTER_IMG_FMT_JPG: return "JPG";
	case ASTER_IMG_FMT_PNG: return "PNG";
	default: return "?";
	}
}

/* ---------------- rendering ----------------
 *
 * Everything is drawn straight into the surface back buffer; the frame's
 * single aster_surface_present() ships all of it. That keeps the HUD strip
 * and the exit box from needing scratch buffers of their own — a fixed
 * strip sized for one panel's width was exactly the trap (it was indexed
 * with the runtime width), and a surface-sized static is out of budget. */

static void draw_hud(aster_gfx_t *g, const char *text)
{
	aster_gfx_fill_rect(g, 0, H - HUD_H, W, HUD_H, ASTER_RGB565(8, 12, 24));
	aster_gfx_text(g, 2, H - HUD_H + 4, text, ASTER_RGB565(235, 238, 245), 0, 0);
}

static void draw_exit_x(aster_gfx_t *g)
{
	uint32_t x = W - EXIT_W;

	aster_gfx_fill_rect(g, x, 0, EXIT_W, EXIT_H, ASTER_RGB565(8, 12, 24));
	aster_gfx_frame_rect(g, x, 0, EXIT_W, EXIT_H, ASTER_RGB565(90, 100, 120));
	aster_gfx_line(g, (int32_t)x + 18, 12, (int32_t)x + 30, 26,
		     ASTER_RGB565(235, 238, 245));
	aster_gfx_line(g, (int32_t)x + 30, 12, (int32_t)x + 18, 26,
		     ASTER_RGB565(235, 238, 245));
}

static const char *base_of(const char *p)
{
	const char *best = p;
	for (const char *s = strchr(p, '/'); s; s = strchr(s + 1, '/')) {
		best = s + 1;
	}
	return best;
}

/* aster_img_render packs the raster at the base of the destination buffer (its
 * pitch is out_w); move it to its centred rect and blank the rest of the
 * image area. Rows are copied last-first so a row's new home never lands on a
 * source row that has not been moved yet; the four blanks then cover every
 * pixel the image rect does not, which is what erases the packed source's
 * remains — it sits at odd angles to the centred rect, so whatever the move
 * does not overwrite would otherwise show as a ghost of the decode. */
static void paint_image(aster_gfx_t *g, uint32_t iw, uint32_t ih)
{
	const uint32_t stride = PITCH / 2; /* surface row pitch, in pixels */
	const uint32_t x = (W - iw) / 2;
	const uint32_t y = (H - HUD_H - ih) / 2;

	for (uint32_t i = ih; i-- > 0;) {
		memmove(&s_scr[(size_t)(y + i) * stride + x], &s_scr[(size_t)i * iw],
			(size_t)iw * 2u);
	}
	aster_gfx_fill_rect(g, 0, 0, W, y, ASTER_BLACK);                  /* above */
	aster_gfx_fill_rect(g, 0, y + ih, W, H - HUD_H - (y + ih), ASTER_BLACK); /* below */
	aster_gfx_fill_rect(g, 0, y, x, ih, ASTER_BLACK);                 /* left */
	aster_gfx_fill_rect(g, x + iw, y, W - (x + iw), ih, ASTER_BLACK); /* right */
}

/* Paint one coherent frame — backdrop, image (when `decode`), HUD line, exit
 * box — and hand the WHOLE surface to the display in one present.
 *
 * The full present is what keeps the firmware's record of the glass valid
 * (a partial write drops it, and `aster ui screenshot` reads that record).
 * Painting every pixel of the surface on every redraw — the clear below, plus
 * paint_image()'s blanks for the image case — is what keeps a previously shown
 * larger image from leaving its edges behind a smaller successor. Redraws
 * happen only when the view changes (s_dirty), not once per tick, so the
 * present's full-frame byte-swap is paid per page flip, not per 5 fps tick.
 *
 * `decode` is 0 for the launch-time frame, which shows the HUD + exit box at
 * once rather than blocking app start on a multi-second decode. */
static void show_current(int decode)
{
	char line[GAL_PATH + 24];
	aster_gfx_t g;

	aster_gfx_init(&g, s_scr, W, H, PITCH);
	aster_gfx_clear(&g, ASTER_BLACK);

	if (s_cnt == 0) {
		aster_snprintf(line, sizeof(line), "no images in /sd");
	} else if (!decode) {
		aster_snprintf(line, sizeof(line), "%d images", s_cnt);
	} else {
		aster_img_render_args_t a;
		int32_t rc;

		memset(&a, 0, sizeof(a));
		a.size = sizeof(a);
		a.version = 1;
		a.dst_off = (uint32_t)(uintptr_t)s_scr; /* wasm32: ptr == offset */
		a.dst_span = W * H * 2u;
		a.cap_w = W;
		a.cap_h = H - HUD_H;
		rc = aster_img_render(s_names[s_idx], (uint32_t)strlen(s_names[s_idx]),
				    &a, sizeof(a));

		if (rc == ASTER_OK && a.out_w && a.out_h) {
			aster_img_info_t i;
			const char *tag = "?";

			paint_image(&g, a.out_w, a.out_h);
			if (aster_img_probe(s_names[s_idx],
					  (uint32_t)strlen(s_names[s_idx]), &i,
					  sizeof(i)) == ASTER_OK) {
				tag = fmt_name(&i);
			}
			aster_snprintf(line, sizeof(line), "%d/%d %s %s %ux%u",
				     s_idx + 1, s_cnt, tag, base_of(s_names[s_idx]),
				     (unsigned)a.out_w, (unsigned)a.out_h);
		} else {
			aster_img_info_t i;
			const char *why = "decode failed";

			if (aster_img_probe(s_names[s_idx],
					  (uint32_t)strlen(s_names[s_idx]), &i,
					  sizeof(i)) == ASTER_OK && i.why[0]) {
				why = i.why;
			}
			aster_snprintf(line, sizeof(line), "%d/%d %s (%d) %s",
				     s_idx + 1, s_cnt, base_of(s_names[s_idx]),
				     (int)rc, why);
		}
	}
	draw_hud(&g, line);
	draw_exit_x(&g);
	aster_surface_present(s_surf);
}

/* ---------------- lifecycle ---------------- */

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = {
		.size = sizeof(d), .version = 1, .format = ASTER_PIXEL_RGB565
	};
	aster_surface_buffer_t b = { .size = sizeof(b), .version = 1 };
	int32_t err = aster_surface_create(&d, sizeof(d), &s_surf);
	if (err != ASTER_OK) {
		return err;
	}
	err = aster_surface_acquire(s_surf, &b, sizeof(b));
	if (err != ASTER_OK) {
		return err;
	}
	W = b.width;
	H = b.height;
	PITCH = b.pitch; /* bytes per row (RGB565 surface: W*2) */
	s_scr = (uint16_t *)(uintptr_t)b.ptr;
	/* The HUD strip and the exit box are laid out against the live width,
	 * so a panel that cannot hold both must fail here instead of wrapping
	 * W - EXIT_W / H - HUD_H into a huge coordinate (which would also turn
	 * every touch into an exit-box hit). */
	if (W < EXIT_W || H <= HUD_H) {
		aster_logf(ASTER_LOG_ERROR, "gallery: %ux%u panel too small for chrome",
			 (unsigned)W, (unsigned)H);
		return ASTER_ENOTSUP;
	}
	aster_app_set_framerate(5); /* viewer: ticks only drive deferred renders */
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_start(void)
{
	scan_all();
	/* Coherent first frame right away (HUD + exit box, no image); the
	 * first image lands on the next frame tick, so app start never waits
	 * out a decode. */
	show_current(0);
	s_dirty = 1;
	aster_logf(ASTER_LOG_INFO, "gallery: %d images", s_cnt);
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	if (ev->type == ASTER_EVENT_FRAME) {
		if (s_dirty) {
			s_dirty = 0;
			show_current(1);
		}
		return;
	}
	if (ev->type != ASTER_EVENT_TOUCH) {
		return;
	}
	if (ev->u.touch.action == ASTER_TOUCH_DOWN) {
		s_down_x = (int32_t)ev->u.touch.x;
		s_down_y = (int32_t)ev->u.touch.y;
		s_down_valid = 1;
		return;
	}
	if (ev->u.touch.action != ASTER_TOUCH_UP || !s_down_valid) {
		return;
	}
	s_down_valid = 0;
	int32_t dx = (int32_t)ev->u.touch.x - s_down_x;
	int32_t dy = (int32_t)ev->u.touch.y - s_down_y;

	/* exit box (top-right) */
	if (ev->u.touch.x >= W - EXIT_W && ev->u.touch.y <= EXIT_H) {
		aster_app_exit(0);
		return;
	}
	if (s_cnt == 0) {
		return;
	}
	/* A pending render (s_dirty) means the previous flip is still decoding;
	 * re-paging here is harmless -- show_current() runs on the frame tick. */
	if (dx >= (int32_t)SWIPE_PX) {
		s_idx = (s_idx + s_cnt - 1) % s_cnt;   /* right swipe = prev */
		s_dirty = 1;
	} else if (dx <= -(int32_t)SWIPE_PX) {
		s_idx = (s_idx + 1) % s_cnt;           /* left swipe = next */
		s_dirty = 1;
	} else if (dx * dx + dy * dy < 400) {
		s_idx = (s_idx + 1) % s_cnt;           /* tap = next */
		s_dirty = 1;
	}
}
