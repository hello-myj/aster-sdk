/* SPDX-License-Identifier: MIT
 * aster.reader — plain-text reader for the SD card, built on the gfx engine.
 *
 * Two screens: a browse list of directories and .txt/.lrc files under /sd,
 * and a paginated view of whichever one you open. There is no wasm widget
 * runtime any more (ABI 3.0) and no firmware-side paging either: the app
 * lays the text out itself, which is why sdk/lib/aster/aster_text.h exists —
 * line height and ascent are not queryable over the ABI.
 *
 * LAYOUT
 * The file is never fully resident. An 8 KB window is re-read from the card
 * whenever the wrap cursor walks out of it, and the wrap is restarted at a
 * line boundary each time, so a page can straddle any number of window
 * refills at no cost beyond the SD reads. Backwards paging pops a stack of
 * byte offsets rather than re-wrapping from byte 0: the offsets are absolute
 * file positions, so they stay valid when the font size changes.
 *
 * PAGE GEOMETRY
 * A face's first baseline sits `asc` below the block top and each further
 * line adds `line_h` (both from aster_text_face). So a page holds
 * `body_h / line_h` lines and the last descender stays inside the body.
 * Changing the font size re-wraps from the same byte offset — the reader
 * stays on the sentence you were reading.
 *
 * DRAWING
 * Panel canvas, like the games: full repaint per present (aster_ge_clear), and
 * an idle frame draws nothing at all. Text goes out as engine commands so the
 * firmware's cpfont cache does the glyph work.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster_text.h>

#define WIN        8192u	/* file window, bytes */
#define ANCHOR_N   128		/* page starts kept for backwards paging */
#define LIST_N     48		/* browse entries */
#define NAME_N     64
#define PATH_N     160
#define LINE_BUF   256		/* one visual line, UTF-8 bytes */

#define TOP_H      28
#define BODY_Y     (TOP_H + 6)
#define PAD_X      8
#define LIST_Y     (TOP_H + 8)
#define TOP_BASE   (ASTER_TEXT_FACES[0].asc)	/* first baseline in the bar */

/* One line of body text at the single face needs asc 21 + about 5 px of
 * descender. ROW_H 30 left the browse rows a 26 px panel, which a 20 px face
 * overflows by 4 px, so the row grew until the panel fits the ink. One fewer
 * row is visible; the scrollbar already handles that. */
#define ROW_H      34


#define BACK_W     34u		/* top-left tap target */

/* Palette carried over from the retired ui.json reader so the app still
 * looks like itself: #0A0E1A / #131826 / #E6EBF5 / #5A6478 / #06B6D4. */
#define COL_BG    ASTER_RGB565(10, 14, 26)
#define COL_PANEL ASTER_RGB565(19, 24, 38)
#define COL_FG    ASTER_RGB565(230, 235, 245)
#define COL_DIM   ASTER_RGB565(90, 100, 120)
#define COL_ACC   ASTER_RGB565(6, 182, 212)
#define COL_WARN  ASTER_RGB565(248, 113, 113)

/* There is one baked face and the ABI clamps everything onto it, so a size
 * ladder is no longer a choice -- 12 through 24 would all rasterize the same
 * 20 px glyphs. The ladder, the tap target that stepped through it and the
 * -/+ key bindings are all gone. What is left is the one size, named here
 * rather than spelled as a literal so it tracks a re-bake on its own. */
#define READ_PX    (ASTER_TEXT_FACES[0].px)

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[160];
static uint8_t arena[1024];
static uint32_t W, H;

/* Rows of the browse list that fit between its top and the bottom edge.
 *
 * This is a function, not the `#define ((320 - LIST_Y) / ROW_H)` it used to
 * be. That literal was arithmetic about one panel: on a 240-tall screen it
 * claims 8 rows where 6 fit, and every use of it is silently wrong rather
 * than visibly broken -- the scrollbar is sized against a list two rows
 * longer than the glass, and the selection can walk into rows that are
 * drawn past the bottom edge. Nothing crashes and nothing looks off; the
 * list just cannot be scrolled to its own end. */
static int list_vis(void)
{
	int32_t v = ((int32_t)H - LIST_Y) / ROW_H;

	return v > 0 ? (int)v : 0;
}

/* browse */
static char cwd[PATH_N] = "/sd";
static char names[LIST_N][NAME_N];
static uint8_t isdir_[LIST_N];
static int n_items, scroll, sel;

/* read */
static aster_handle_t fh = ASTER_HANDLE_INVALID;
static char title[NAME_N];
static uint32_t fsize, page_off, next_off;
static uint32_t anchor[ANCHOR_N];
static int anchor_n;

static char win[WIN + 1];
static uint32_t win_base, win_len;
static char lbuf[LINE_BUF];

static int mode;		/* 0 browse, 1 read */
static int dirty = 1;
static int32_t down_x = -1, down_y = -1;

/* ---------------- small text helpers ---------------- */

static void text_at(int32_t x, int32_t base, uint8_t px, uint16_t fg,
		    const char *s)
{
	aster_ge_text(&ge, x, base, px, fg, ASTER_GFX_NOBG, s);
}

static void text_center(int32_t cx, int32_t base, uint8_t px, uint16_t fg,
			const char *s)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w < 0) {
		return;
	}
	text_at(cx - w / 2, base, px, fg, s);
}

static void text_right(int32_t rx, int32_t base, uint8_t px, uint16_t fg,
		       const char *s)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w < 0) {
		return;
	}
	text_at(rx - w, base, px, fg, s);
}

/* Largest suffix of s that fits `maxw`, cut on a character boundary. Used
 * for the path in the top bar, where the tail is the informative half. */
static const char *tail_fit(uint8_t px, const char *s, int32_t maxw)
{
	uint32_t len = (uint32_t)strlen(s);
	uint16_t starts[PATH_N + 1];
	uint32_t i = 0, n = 0;

	while (i < len && n < PATH_N) {
		starts[n++] = (uint16_t)i;
		i += aster_utf8_len(s + i);
	}
	starts[n] = (uint16_t)len;
	/* Longest first: starts[0] is the whole string, so a path that fits is
	 * returned untouched and only an overlong one gets its head cut. */
	for (i = 0; i < n; i++) {
		if (aster_text_measure(px, s + starts[i], len - starts[i]) <= maxw) {
			return s + starts[i];
		}
	}
	return "";
}

/* ---------------- file window ---------------- */

static void win_load(uint32_t off)
{
	int32_t n;

	win_base = off;
	win_len = 0;
	if (fh == ASTER_HANDLE_INVALID || off >= fsize) {
		return;
	}
	if (aster_fs_seek(fh, (int32_t)off, ASTER_FS_SEEK_SET) != ASTER_OK) {
		return;
	}
	n = aster_fs_read(fh, win, WIN);
	if (n <= 0) {
		return;
	}
	win_len = (uint32_t)n;
	if (win_base + win_len > fsize) {
		win_len = fsize - win_base;	/* card lies; trust fsize */
	}
}

static void win_ensure(uint32_t off)
{
	if (win_len && off >= win_base && off < win_base + win_len) {
		return;
	}
	win_load(off);
}

/* ---------------- browse ---------------- */

static int is_doc(const char *name)
{
	size_t n = strlen(name);

	if (n < 4 || name[n - 4] != '.') {
		return 0;
	}
	{
		char a = (char)(name[n - 3] | 32);
		char b = (char)(name[n - 2] | 32);
		char c = (char)(name[n - 1] | 32);

		return (a == 't' && b == 'x' && c == 't') ||
		       (a == 'l' && b == 'r' && c == 'c');
	}
}

/* Insertion sort, files before directories, each group by name. Documents
 * first on purpose: this is a reader, so the likeliest target sits at the
 * top where the cursor starts and navigation is the second-class action. */
static void sort_items(void)
{
	int i, j;

	for (i = 1; i < n_items; i++) {
		char key[NAME_N];
		uint8_t kd = isdir_[i];

		memcpy(key, names[i], NAME_N);
		for (j = i - 1; j >= 0; j--) {
			int ord = 0;

			if (kd != isdir_[j]) {
				ord = kd ? -1 : 1;
			} else {
				ord = strcmp(names[j], key);
			}
			if (ord <= 0) {
				break;
			}
			memcpy(names[j + 1], names[j], NAME_N);
			isdir_[j + 1] = isdir_[j];
		}
		memcpy(names[j + 1], key, NAME_N);
		isdir_[j + 1] = kd;
	}
}

static void scan_dir(void)
{
	aster_handle_t dh = ASTER_HANDLE_INVALID;
	char name[NAME_N];
	uint32_t d;

	n_items = 0;
	scroll = 0;
	sel = 0;
	if (aster_fs_opendir(cwd, (uint32_t)strlen(cwd), &dh) != ASTER_OK) {
		return;
	}
	while (n_items < LIST_N) {
		int32_t n = aster_fs_dir_next(dh, name, sizeof(name), &d);

		if (n <= 0) {
			break;
		}
		if (name[0] == '.') {
			continue;
		}
		if (!d && !is_doc(name)) {
			continue;
		}
		memcpy(names[n_items], name, NAME_N - 1);
		names[n_items][NAME_N - 1] = 0;
		isdir_[n_items] = (uint8_t)(d ? 1 : 0);
		n_items++;
	}
	aster_fs_closedir(dh);
	sort_items();
}

static void path_push(const char *name)
{
	if (strcmp(cwd, "/sd") != 0) {
		aster_snprintf(cwd + strlen(cwd), PATH_N - strlen(cwd), "/%s", name);
	} else {
		aster_snprintf(cwd, PATH_N, "/sd/%s", name);
	}
}

static void path_pop(void)
{
	char *p = cwd;
	char *slash = NULL;

	while (*p) {
		if (*p == '/') {
			slash = p;
		}
		p++;
	}
	if (!slash || slash == cwd) {
		return;
	}
	*slash = 0;
	if (!cwd[0] || (cwd[0] == '/' && cwd[1] && !cwd[2])) {
		aster_snprintf(cwd, PATH_N, "/sd");
	}
}

static void open_doc(const char *name)
{
	char path[PATH_N];
	aster_stat_t st = { .size = sizeof(st), .version = 1 };

	if (strcmp(cwd, "/sd") == 0) {
		aster_snprintf(path, PATH_N, "/sd/%s", name);
	} else {
		aster_snprintf(path, PATH_N, "%s/%s", cwd, name);
	}
	if (aster_fs_stat(path, (uint32_t)strlen(path), &st, sizeof(st)) != ASTER_OK) {
		return;
	}
	if (aster_fs_open(path, (uint32_t)strlen(path), ASTER_FS_O_RDONLY, &fh) !=
	    ASTER_OK) {
		fh = ASTER_HANDLE_INVALID;
		return;
	}
	memcpy(title, name, NAME_N - 1);
	title[NAME_N - 1] = 0;
	fsize = (uint32_t)st.filesize;
	page_off = 0;
	next_off = 0;
	anchor_n = 0;
	win_len = 0;
	mode = 1;
	dirty = 1;
	aster_logf(ASTER_LOG_INFO, "reader: %s %u B", path, (unsigned)fsize);
}

static void close_doc(void)
{
	if (fh != ASTER_HANDLE_INVALID) {
		aster_fs_close(fh);
		fh = ASTER_HANDLE_INVALID;
	}
	mode = 0;
	dirty = 1;
}

/* ---------------- page rendering ---------------- */

static void draw_top(void)
{
	char buf[24];

	aster_ge_rect(&ge, 0, 0, W, TOP_H, COL_PANEL);
	/* back chevron: a triangle, so it does not depend on a glyph */
	aster_ge_tri(&ge, 22, 7, 22, 21, 10, 14, COL_ACC);

	/* Baseline 21, not 20: at the single face the ascent is 21, so a
	 * baseline of 20 puts the top of the ink one row above a 28 px bar.
	 * 21 is the largest ascent the face can have and still fit. */
	if (mode == 1) {
		/* Title keeps the TAIL of the filename: the distinguishing part
		 * is the end, and the head is what the eye can lose. The room it
		 * gets is the whole bar minus the chevron -- it used to stop
		 * short of a size readout in the top-right corner, and that
		 * readout is gone with the size ladder. */
		text_at(34, TOP_BASE, READ_PX, COL_FG,
			tail_fit(READ_PX, title, (int32_t)W - 34 - PAD_X));
		aster_snprintf(buf, sizeof(buf), "%ld%%",
			     (long)(fsize ? (page_off * 100u) / fsize : 0u));
		text_right((int32_t)W - PAD_X, TOP_BASE, READ_PX, COL_DIM, buf);
		/* progress: how far into the file this page starts */
		if (fsize) {
			int32_t pw = (int32_t)((uint64_t)W * page_off / fsize);

			if (pw > 1) {
				aster_ge_rect(&ge, 0, TOP_H - 2, (uint32_t)pw, 2,
					    COL_ACC);
			}
		}
	} else {
		text_at(34, TOP_BASE, READ_PX, COL_FG,
			tail_fit(READ_PX, cwd, (int32_t)W - 34 - PAD_X));
	}
}

static int32_t draw_read(void)
{
	int32_t lh = aster_text_line_h(READ_PX);
	int32_t asc = aster_text_asc(READ_PX);
	int32_t maxw = (int32_t)W - 2 * PAD_X;
	int32_t maxl = lh > 0 ? ((int32_t)H - BODY_Y) / lh : 0;
	uint32_t pos = page_off;
	int32_t n = 0;

	aster_ge_clear(&ge, COL_BG);
	draw_top();

	if (!aster_text_ready(READ_PX)) {
		/* The rasterizer draws nothing and returns success when the
		 * cpfont faces are not resident, so a missing SD card would
		 * otherwise look like an empty file. Say so instead. */
		text_at(PAD_X, BODY_Y + asc, READ_PX, COL_WARN, "字体未就绪");
		text_at(PAD_X, BODY_Y + asc + lh, READ_PX, COL_DIM,
			"插卡后重启设备");
		next_off = page_off;
		return aster_ge_present(&ge);
	}

	while (n < maxl && pos < fsize) {
		uint32_t off, avail, llen, next, cpy;

		win_ensure(pos);
		off = pos - win_base;
		if (off >= win_len) {
			break;
		}
		avail = win_len - off;
		next = aster_text_line(READ_PX, win + off, avail, maxw, &llen);
		if (!next && !llen) {
			break;		/* nothing consumed: stop, do not spin */
		}
		if (llen) {
			cpy = llen < LINE_BUF - 1u ? llen : LINE_BUF - 1u;
			memcpy(lbuf, win + off, cpy);
			lbuf[cpy] = 0;
			text_at(PAD_X, BODY_Y + asc + n * lh, READ_PX, COL_FG,
				lbuf);
		}
		n++;
		pos += next;
	}
	next_off = pos;

	if (!n && !fsize) {
		text_center((int32_t)W / 2, BODY_Y + asc + lh, READ_PX, COL_DIM,
			    "空文件");
	}
	return aster_ge_present(&ge);
}

static int32_t draw_browse(void)
{
	char buf[NAME_N + 4];
	int i;

	aster_ge_clear(&ge, COL_BG);
	draw_top();

	if (!n_items) {
		text_center((int32_t)W / 2, (int32_t)H / 2, READ_PX, COL_DIM,
			    "没有文本文件");
		text_center((int32_t)W / 2,
			    (int32_t)H / 2 + aster_text_line_h(READ_PX), READ_PX,
			    COL_DIM, "把 .txt / .lrc 放到卡上");
		return aster_ge_present(&ge);
	}

	for (i = 0; i < list_vis() && scroll + i < n_items; i++) {
		int k = scroll + i;
		int32_t y = LIST_Y + i * ROW_H;
		int on = (k == sel);
		const char *shown = names[k];

		aster_ge_rect_r(&ge, PAD_X, y, W - 2 * PAD_X, ROW_H - 4, COL_PANEL,
			      6, on ? 1 : 0);
		if (on) {
			/* outline the cursor row; the fill still says what it is */
			aster_ge_rect_r(&ge, PAD_X, y, W - 2 * PAD_X, ROW_H - 4,
				      COL_ACC, 6, 1);
		}
		if (isdir_[k]) {
			aster_snprintf(buf, sizeof(buf), "%s/", names[k]);
		} else {
			aster_snprintf(buf, sizeof(buf), "%s", names[k]);
		}
		if (aster_text_elide(READ_PX, buf, (uint32_t)strlen(buf),
				   (int32_t)W - 2 * PAD_X - 24, lbuf,
				   LINE_BUF) >= 0) {
			shown = lbuf;
		}
		/* Baseline at the panel top + asc, so the ink starts flush with
		 * the rounded rect instead of floating in the middle of it. */
		text_at(PAD_X + 8, y + aster_text_asc(READ_PX), READ_PX,
			isdir_[k] ? COL_ACC : COL_FG, shown);
	}
	/* scrollbar, only when the list does not fit */
	if (n_items > list_vis()) {
		int32_t track = (int32_t)H - LIST_Y;
		int32_t bar = track * list_vis() / n_items;
		int32_t off = track * scroll / n_items;

		if (bar < 8) {
			bar = 8;
		}
		aster_ge_rect(&ge, W - 3, LIST_Y + off, 3, (uint32_t)bar, COL_ACC);
	}
	return aster_ge_present(&ge);
}

static int32_t draw(void)
{
	return mode == 1 ? draw_read() : draw_browse();
}

/* ---------------- input ---------------- */

static int hit_top_back(int32_t x, int32_t y)
{
	return y < TOP_H && x < (int32_t)BACK_W;
}

static void page(int dir)
{
	if (dir > 0) {
		if (next_off >= fsize) {
			return;
		}
		if (anchor_n < ANCHOR_N) {
			anchor[anchor_n++] = page_off;
		} else {
			/* Stack full: drop the older half. Backwards paging
			 * then stops earlier, which beats unbounded growth. */
			memmove(anchor, anchor + ANCHOR_N / 2,
				(ANCHOR_N / 2) * sizeof(anchor[0]));
			anchor_n = ANCHOR_N / 2;
			anchor[anchor_n++] = page_off;
		}
		page_off = next_off;
	} else {
		if (anchor_n <= 0) {
			if (page_off == 0) {
				return;
			}
			page_off = 0;
		} else {
			page_off = anchor[--anchor_n];
		}
	}
	dirty = 1;
}

/* Move the browse cursor, keeping it on screen. */
static void sel_move(int dir)
{
	int maxs;

	if (!n_items) {
		return;
	}
	sel += dir;
	if (sel < 0) {
		sel = 0;
	}
	if (sel > n_items - 1) {
		sel = n_items - 1;
	}
	if (sel < scroll) {
		scroll = sel;
	}
	if (sel >= scroll + list_vis()) {
		scroll = sel - list_vis() + 1;
	}
	maxs = n_items > list_vis() ? n_items - list_vis() : 0;
	if (scroll > maxs) {
		scroll = maxs;
	}
	dirty = 1;
}

static void sel_open(void)
{
	if (!n_items || sel < 0 || sel >= n_items) {
		return;
	}
	if (isdir_[sel]) {
		path_push(names[sel]);
		scan_dir();
		dirty = 1;
		return;
	}
	open_doc(names[sel]);
}

static void tap_browse(int32_t y)
{
	int i;

	if (y < LIST_Y) {
		return;
	}
	i = (int32_t)((y - LIST_Y) / ROW_H);
	if (i < 0 || i >= list_vis() || scroll + i >= n_items) {
		return;
	}
	sel = scroll + i;
	sel_open();
}

static void on_touch(const aster_event_t *ev)
{
	int32_t x = (int32_t)ev->u.touch.x;
	int32_t y = (int32_t)ev->u.touch.y;

	if (ev->u.touch.action == ASTER_TOUCH_DOWN) {
		down_x = x;
		down_y = y;
		return;
	}
	if (ev->u.touch.action != ASTER_TOUCH_UP) {
		return;
	}

	if (down_x >= 0) {
		int32_t dx = x - down_x;
		int32_t dy = y - down_y;
		int32_t adx = dx < 0 ? -dx : dx;
		int32_t ady = dy < 0 ? -dy : dy;

		down_x = down_y = -1;
		if (hit_top_back(x, y)) {
			if (mode == 1) {
				close_doc();
			} else if (strcmp(cwd, "/sd") != 0) {
				path_pop();
				scan_dir();
				dirty = 1;
			}
			return;
		}
		if (adx > 32 || ady > 32) {	/* swipe */
			if (mode == 1) {
				if (adx >= ady) {
					page(dx < 0 ? 1 : -1);
				} else {
					page(dy < 0 ? 1 : -1); /* up = forward */
				}
			} else if (ady > adx) {
				int32_t maxs = n_items > list_vis() ? n_items - list_vis() : 0;
				int32_t s = scroll + (dy < 0 ? 1 : -1);

				if (s < 0) {
					s = 0;
				}
				if (s > maxs) {
					s = maxs;
				}
				if (s != scroll) {
					scroll = (int)s;
					dirty = 1;
				}
			}
			return;
		}
		/* tap */
		if (mode == 1) {
			if (x > (int32_t)W / 3) {
				page(1);
			} else {
				page(-1);
			}
		} else {
			tap_browse(y);
		}
	}
}

/* Keyboard, for boards that have one and for `hw key inject` in tests. The
 * ABI forwards raw evdev codes; only the handful below are given meaning. */
#define K_ESC   1u
#define K_ENTER 28u
#define K_SPACE 57u
#define K_UP    103u
#define K_LEFT  105u
#define K_RIGHT 106u
#define K_DOWN  108u

static void key_back(void)
{
	if (mode == 1) {
		close_doc();
	} else if (strcmp(cwd, "/sd") != 0) {
		path_pop();
		scan_dir();
		dirty = 1;
	}
}

static void on_key(const aster_event_t *ev)
{
	uint32_t code = ev->u.key.code;

	if (!ev->u.key.pressed) {
		return;
	}
	switch (code) {
	case K_ESC:
		key_back();
		break;
	case K_ENTER:
	case K_SPACE:
		if (mode == 1) {
			page(1);
		} else {
			sel_open();
		}
		break;
	case K_DOWN:
	case K_RIGHT:
		if (mode == 1) {
			page(1);
		} else {
			sel_move(1);
		}
		break;
	case K_UP:
	case K_LEFT:
		if (mode == 1) {
			page(-1);
		} else {
			sel_move(-1);
		}
		break;
	default:
		break;
	}
}

ASTER_APP_EXPORT int aster_app_init(void)
{
	aster_surface_desc_t d = {
		.size = sizeof(d), .version = 1, .format = ASTER_PIXEL_NATIVE,
		.flags = ASTER_SURFACE_F_PANEL_CANVAS,
	};
	aster_surface_buffer_t buf = { .size = sizeof(buf), .version = 1 };
	int32_t err;

	aster_app_set_framerate(20);
	err = aster_surface_create(&d, sizeof(d), &surf);
	if (err != ASTER_OK) {
		aster_logf(ASTER_LOG_ERROR, "reader surface: %d", err);
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

	scan_dir();
	aster_logf(ASTER_LOG_INFO, "reader %ux%u %d items %upx canvas=%d", W, H,
		 n_items, (unsigned)READ_PX,
		 (buf.flags & ASTER_SURFACE_F_PANEL_CANVAS) != 0u);
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	close_doc();
	if (surf != ASTER_HANDLE_INVALID) {
		aster_surface_destroy(surf);
		surf = ASTER_HANDLE_INVALID;
	}
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
	if (ev->type == ASTER_EVENT_TOUCH) {
		on_touch(ev);
	} else if (ev->type == ASTER_EVENT_KEY) {
		on_key(ev);
	} else if (ev->type == ASTER_EVENT_FRAME) {
		/* Content changes are ours; a filled-in glyph is the platform's.
		 * Baked bitmaps arrive asynchronously, so a page that paints once
		 * shows blanks until it repaints -- see aster_gfx_fonts_pending. */
		if (dirty || aster_gfx_fonts_pending()) {
			dirty = 0;
			draw();
		}
	}
}
