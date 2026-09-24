/* SPDX-License-Identifier: MIT
 * aster.player — audio player for /music (or /sd), built on the gfx engine.
 *
 * Three screens: the library list, the now-playing chrome (title, progress,
 * transport, volume) and the lyrics. Replaces the retired aster.music, which
 * was written against the wasm widget runtime that ABI 3.0 deleted.
 *
 * WHAT THE ABI GIVES US, AND WHAT IT DOES NOT
 * Playback is fully firmware-side: aster_audio_start_file streams and decodes
 * (WAV/PCM/MP3), aster_audio_get_state reports position in ms, and
 * aster_audio_seek jumps. There is no ID3 tag API and no album art, so the
 * title is the FILENAME — the only metadata the app can actually see.
 * Lyrics do come over the ABI (aster_audio_get_lyrics): ID3 USLT/SYLT when the
 * file has them, else a sibling .lrc, and for a .lrc the '[mm:ss.xx]' tags
 * are returned verbatim, so the app parses them itself and can highlight.
 *
 * REDRAWS
 * The frame tick polls aster_audio_get_state and redraws only when something
 * visible changed: transport state, whole-second position, volume, or the
 * highlighted lyric line. A paused player with no touch input draws nothing.
 */
#include <aster_imports.h>
#include <aster_gfx.h>
#include <aster_gfx_engine.h>
#include <aster/audio.h>
#include <aster_text.h>

#define LIST_N     64
#define NAME_N     64
#define PATH_N     160

#define LYR_BUF    6144u	/* firmware caps its own blob at 12288 */
#define LYR_N      96

/* There is one baked face and the ABI clamps every size onto it, so the
 * size arguments below are all the same thing. They used to be three
 * different numbers (16 for content, 14 for the bar, 12 for the small
 * print) and each was a separate guess about how much room it had. */
#define PLAY_PX    (ASTER_TEXT_FACES[0].px)
#define PLAY_ASC   ((int32_t)ASTER_TEXT_FACES[0].asc)   /* first baseline */
#define PLAY_LH    ((int32_t)ASTER_TEXT_FACES[0].line_h) /* baseline step */

#define TOP_H      28
#define TOP_BASE   PLAY_ASC	/* largest ascent that still fits TOP_H */
#define LIST_Y     (TOP_H + 8)
/* One line at the single face is asc 21 + ~5 descender; ROW_H 30 left a
 * 26 px panel and the ink ran 4 px past it. */
#define ROW_H      34

/* Lyric lines are BASELINE positions, so the count has to come from the
 * panel height divided by the real line step -- it was a literal 13 sized
 * for the old 21 px step, and at 26 that would have run the last three
 * lines off the bottom of the screen. Computed at use, not defined, because
 * the face metrics are not integer constant expressions. */
#define LYR_TOP    (TOP_H + 10)
#define LYR_LINE   96		/* longest stored lyric line, UTF-8 bytes */

/* Transport geometry (240x320). */
#define PB_CX      120
#define PB_CY      252
#define PB_R       30
/* The now-playing page stacks downward: cover (36..160), title, progress bar
 * (PBAR_Y .. PBAR_Y + PBAR_H - 1), time labels, transport (PB_CY +/- PB_R =
 * 222..282), volume (VOL_Y .. VOL_Y + 5 = 296..301).
 *
 * At the single face the title ink is 21 px above its baseline and the time
 * labels put theirs at TIME_BASE, so the bar and the labels no longer fit in
 * the 30 px the old 12 px labels needed between the title and the transport:
 * the labels' ink now reaches ~224 and the transport circle starts at 222.
 * They clear each other because the labels sit at the far left and right
 * (measured on the glass: 0..100 and 140..240) while the transport is
 * centred -- no accent-coloured pixel lands in rows 208..224. */
#define PBAR_H     8
#define PBAR_Y     192
#define TIME_BASE  (PBAR_Y + PBAR_H + 2 + PLAY_ASC)
#define VOL_Y      296
#define VOL_BAND   290		/* tap band top */

/* Palette carried over from the retired ui.json player so it still looks
 * like itself: #2C3A3E / #3D4C50 / #FFFFFF / #E8C547 / #D45A4A. */
#define COL_BG    ASTER_RGB565(44, 58, 62)
#define COL_PANEL ASTER_RGB565(61, 76, 80)
#define COL_FG    ASTER_RGB565(255, 255, 255)
#define COL_DIM   ASTER_RGB565(154, 168, 172)
#define COL_ACC   ASTER_RGB565(232, 197, 71)
#define COL_TRACK ASTER_RGB565(30, 42, 46)
#define COL_ERR   ASTER_RGB565(212, 90, 74)

static aster_handle_t surf = ASTER_HANDLE_INVALID;
static aster_ge_t ge;
static aster_gfx_cmd_t ring[96];
static uint8_t arena[512];
static uint32_t W, H;

/* Songs listed between the list top and the bottom edge. A function rather
 * than the `((320 - LIST_Y) / ROW_H)` it used to be: that was arithmetic
 * about one panel, and on a 240-tall screen it claims 8 rows where 6 fit --
 * the scrollbar is sized against a list two rows longer than the glass and
 * the selection walks into rows nobody can see. Same fix as reader's. */
static int list_vis(void)
{
	int32_t v = ((int32_t)H - LIST_Y) / ROW_H;

	return v > 0 ? (int)v : 0;
}

static char root[PATH_N] = "/music";
static char names[LIST_N][NAME_N];
static int n_songs, cur = -1, scroll, sel;

static uint32_t seen_state = 0xFFFFFFFFu;
static int32_t seen_sec = -1;
static int32_t seen_vol = -1;
static int seen_lyr = -2;
static int saw_playing;		/* a PLAYING edge since the last start */
static int fetching;		/* a start_file is in flight */
static int32_t last_ms;		/* position at the last PLAYING report ... */
static int32_t last_total;	/* ... and the duration that went with it */

static char lyr[LYR_BUF];
static uint32_t lyr_ms[LYR_N];
static uint16_t lyr_off[LYR_N];
static uint16_t lyr_len[LYR_N];
static int lyr_n, lyr_for = -1;

static int page;		/* 0 library, 1 now, 2 lyrics */
static int dirty = 1;
static int32_t down_x = -1, down_y = -1;
static char fitbuf[NAME_N];
static char lyline[LYR_LINE + 4];
static char err_line[NAME_N + 24];	/* toast on the library page */
static int err_frames;			/* >0: banner is up, counts down */

/* ---------------- drawing helpers ---------------- */

static void text_at(int32_t x, int32_t base, uint8_t px, uint16_t fg,
		    const char *s)
{
	aster_ge_text(&ge, x, base, px, fg, ASTER_GFX_NOBG, s);
}

static void text_center(int32_t cx, int32_t base, uint8_t px, uint16_t fg,
			const char *s)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w >= 0) {
		text_at(cx - w / 2, base, px, fg, s);
	}
}

static void text_right(int32_t rx, int32_t base, uint8_t px, uint16_t fg,
		       const char *s)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w >= 0) {
		text_at(rx - w, base, px, fg, s);
	}
}

/* Fitting form of `s` for `maxw`, or `s` itself when it already fits. */
static const char *fit(uint8_t px, const char *s, int32_t maxw)
{
	int32_t w = aster_gfx_text_w(px, s, (uint32_t)strlen(s));

	if (w >= 0 && w <= maxw) {
		return s;
	}
	if (aster_text_elide(px, s, (uint32_t)strlen(s), maxw, fitbuf,
			   sizeof(fitbuf)) < 0) {
		return "";
	}
	return fitbuf;
}

static void fmt_time(char *dst, int max, uint32_t ms)
{
	uint32_t s = ms / 1000u;

	aster_snprintf(dst, max, "%u:%02u", (unsigned)(s / 60u),
		     (unsigned)(s % 60u));
}

static void draw_top(const char *name)
{
	aster_ge_rect(&ge, 0, 0, W, TOP_H, COL_PANEL);
	aster_ge_tri(&ge, 22, 7, 22, 21, 10, 14, COL_ACC);
	text_at(34, TOP_BASE, PLAY_PX, COL_FG,
		fit(PLAY_PX, name, (int32_t)W - 34 - 56));
	if (page == 1) {
		text_right((int32_t)W - 8, TOP_BASE, PLAY_PX, COL_ACC, "词");
	} else if (page == 2) {
		text_right((int32_t)W - 8, TOP_BASE, PLAY_PX, COL_ACC, "曲");
	}
}

/* ---------------- library ---------------- */

static int is_audio(const char *name)
{
	size_t n = strlen(name);

	if (n < 4 || name[n - 4] != '.') {
		return 0;
	}
	{
		char a = (char)(name[n - 3] | 32);
		char b = (char)(name[n - 2] | 32);
		char c = (char)(name[n - 1] | 32);

		return (a == 'm' && b == 'p' && c == '3') ||
		       (a == 'w' && b == 'a' && c == 'v') ||
		       (a == 'p' && b == 'c' && c == 'm');
	}
}

static int scan_dir(const char *dir)
{
	aster_handle_t dh = ASTER_HANDLE_INVALID;
	char name[NAME_N];
	uint32_t d;
	int n = 0;

	if (aster_fs_opendir(dir, (uint32_t)strlen(dir), &dh) != ASTER_OK) {
		return -1;
	}
	while (n < LIST_N) {
		int32_t r = aster_fs_dir_next(dh, name, sizeof(name), &d);

		if (r <= 0) {
			break;
		}
		if (d || name[0] == '.' || !is_audio(name)) {
			continue;
		}
		memcpy(names[n], name, NAME_N - 1);
		names[n][NAME_N - 1] = 0;
		n++;
	}
	aster_fs_closedir(dh);
	return n;
}

static void scan_all(void)
{
	int n = scan_dir("/music");

	if (n <= 0) {
		aster_snprintf(root, PATH_N, "/sd");
		n = scan_dir("/sd");
	}
	n_songs = n > 0 ? n : 0;
	scroll = 0;
	sel = 0;
}

/* Keep the selection and the scroll window consistent after either moves. */
static void sel_follow(void)
{
	int maxs;

	if (!n_songs) {
		sel = 0;
		scroll = 0;
		return;
	}
	if (sel < 0) {
		sel = 0;
	}
	if (sel > n_songs - 1) {
		sel = n_songs - 1;
	}
	if (sel < scroll) {
		scroll = sel;
	}
	if (sel >= scroll + list_vis()) {
		scroll = sel - list_vis() + 1;
	}
	maxs = n_songs > list_vis() ? n_songs - list_vis() : 0;
	if (scroll > maxs) {
		scroll = maxs;
	}
}

static void track_path(int i, char *dst, uint32_t max)
{
	aster_snprintf(dst, max, "%s/%s", root, names[i]);
}

/* Start track `i`. `now` says whether to bring up the now-playing screen:
 * true when the user just picked this track, false when the app is moving
 * on its own. The distinction is load-bearing -- a track ending advances
 * while the library may be on screen, and that must not yank the list out
 * from under the finger that is scrolling it. */
static void play(int i, int now)
{
	char path[PATH_N];
	int32_t e;

	if (i < 0 || i >= n_songs) {
		return;
	}
	sel = i;
	sel_follow();
	saw_playing = 0;
	fetching = 1;
	lyr_for = -1;
	track_path(i, path, sizeof(path));
	e = aster_audio_start_file(path, (uint32_t)strlen(path));
	if (e != ASTER_OK) {
		/* A start stops whatever was playing before it looks at the
		 * new path, so a failure leaves the device with no track at
		 * all -- and cur has to say so, or the list keeps a gold
		 * "playing" row for silence. Say why on the glass too: an
		 * unopenable name is the likeliest cause, and the log line it
		 * used to be is not where the user is looking. */
		aster_logf(ASTER_LOG_ERROR, "player: start %s -> %d", path, e);
		aster_snprintf(err_line, sizeof(err_line), "无法播放 %s", names[i]);
		err_frames = 60;	/* ~3 s at the frame tick */
		cur = -1;
		fetching = 0;
	} else {
		cur = i;
		if (now) {
			page = 1;
		}
	}
	dirty = 1;
}

static void transport(void)
{
	aster_audio_state_t st = { .size = sizeof(st), .version = 2 };

	if (aster_audio_get_state(&st, sizeof(st)) != ASTER_OK) {
		return;
	}
	if (st.state == ASTER_AUDIO_PLAYING) {
		aster_audio_pause();
	} else if (st.state == ASTER_AUDIO_PAUSED) {
		aster_audio_resume();
	} else if (cur >= 0) {
		play(cur, 0);	/* replaying: leave the page where it is */
	} else if (n_songs) {
		play(0, 1);
	}
	dirty = 1;
}

static void skip(int dir)
{
	if (!n_songs) {
		return;
	}
	if (cur < 0) {
		play(0, 0);
		return;
	}
	play((cur + dir + n_songs) % n_songs, 0);
}

/* ---------------- lyrics ---------------- */

/* One '[mm:ss.xx]' tag at p. Returns bytes consumed and stores ms in *out,
 * or 0 if there is no tag here. */
static int parse_ts(const char *p, uint32_t *out)
{
	uint32_t mm = 0, ss = 0, xx = 0;
	int nfrac = 0;
	const char *s = p;

	if (*s != '[' || s[1] < '0' || s[1] > '9') {
		return 0;
	}
	s++;
	while (*s >= '0' && *s <= '9') {
		mm = mm * 10u + (uint32_t)(*s - '0');
		s++;
	}
	if (*s != ':') {
		return 0;
	}
	s++;
	while (*s >= '0' && *s <= '9') {
		ss = ss * 10u + (uint32_t)(*s - '0');
		s++;
	}
	if (*s == '.' || *s == ':') {
		s++;
		while (*s >= '0' && *s <= '9' && nfrac < 3) {
			xx = xx * 10u + (uint32_t)(*s - '0');
			nfrac++;
			s++;
		}
		while (nfrac < 3) {
			xx *= 10u;
			nfrac++;
		}
	}
	if (*s != ']') {
		return 0;
	}
	*out = mm * 60000u + ss * 1000u + xx;
	return (int)(s + 1 - p);
}

static void lyrics_parse(void)
{
	const char *p = lyr;
	int n = 0;

	lyr_n = 0;
	while (*p && n < LYR_N) {
		uint32_t ms[4];
		int nts = 0;
		const char *line = p;
		const char *text;

		while (*p == '\r' || *p == '\n') {
			p++;
		}
		if (!*p) {
			break;
		}
		line = p;
		while (nts < 4) {	/* a line may carry several stamps */
			uint32_t t;
			int used = parse_ts(p, &t);

			if (!used) {
				break;
			}
			ms[nts++] = t;
			p += used;
		}
		text = p;
		while (*p && *p != '\n' && *p != '\r') {
			p++;
		}
		if (!nts) {
			/* untimed line (plain lyrics, or a tag we did not
			 * recognise): keep it, sequenced after the last one */
			if (!line[0]) {
				continue;
			}
			text = line;
			ms[0] = lyr_n ? lyr_ms[lyr_n - 1] + 1u : 0u;
			nts = 1;
		}
		{
			uint32_t off = (uint32_t)(text - lyr);
			uint32_t len = (uint32_t)(p - text);

			while (len && (text[len - 1] == ' ' ||
				       text[len - 1] == '\t')) {
				len--;
			}
			/* Store a bounded prefix, cut on a character
			 * boundary: the renderer copies the slice out to
			 * NUL-terminate it, and a cut mid-sequence would
			 * put a broken glyph on the glass. */
			if (len > LYR_LINE) {
				uint32_t c = 0;

				while (c < LYR_LINE) {
					uint32_t k = aster_utf8_len(text + c);

					if (c + k > LYR_LINE) {
						break;
					}
					c += k;
				}
				len = c;
			}
			if (!len) {
				continue;
			}
			lyr_off[n] = (uint16_t)off;
			lyr_len[n] = (uint16_t)len;
			lyr_ms[n] = ms[0];
			n++;
		}
	}
	lyr_n = n;
}

static void fetch_lyrics(void)
{
	int32_t n;

	if (lyr_for == cur) {
		return;
	}
	lyr_for = cur;
	lyr_n = 0;
	if (cur < 0) {
		return;
	}
	n = aster_audio_get_lyrics(lyr, LYR_BUF - 1u);
	if (n > 0) {
		lyr[n] = 0;
		lyrics_parse();
	}
	aster_logf(ASTER_LOG_INFO, "player: lyrics %d lines (%d B)",
		 lyr_n, (int)(n > 0 ? n : 0));
}

/* Index of the line playing at `ms`, or -1. */
static int lyric_at(uint32_t ms)
{
	int i, best = -1;

	for (i = 0; i < lyr_n; i++) {
		if (lyr_ms[i] <= ms) {
			best = i;
		} else {
			break;
		}
	}
	return best;
}

/* ---------------- pages ---------------- */

static void draw_library(void)
{
	char buf[NAME_N + 4];
	int i;

	aster_ge_clear(&ge, COL_BG);
	draw_top("曲库");
	if (!n_songs) {
		text_center((int32_t)W / 2, (int32_t)H / 2, PLAY_PX, COL_DIM,
			    "没有音频文件");
		text_center((int32_t)W / 2, (int32_t)H / 2 + PLAY_LH, PLAY_PX,
			    COL_DIM, "把 mp3 / wav 放到卡上");
		return;
	}
	for (i = 0; i < list_vis() && scroll + i < n_songs; i++) {
		int k = scroll + i;
		int32_t y = LIST_Y + i * ROW_H;
		int playing = (k == cur);

		/* Fill says what is playing, outline says where the cursor is:
		 * the two are independent once you arrow away from the track
		 * that is still sounding. */
		aster_ge_rect_r(&ge, 8, y, W - 16, ROW_H - 4,
			      playing ? COL_ACC : COL_PANEL, 6, 0);
		if (k == sel) {
			aster_ge_rect_r(&ge, 8, y, W - 16, ROW_H - 4, COL_ACC, 6,
				      1);
		}
		aster_snprintf(buf, sizeof(buf), "%s", names[k]);
		/* Baseline at the panel top + asc: flush with the rounded rect
		 * rather than floating inside it. */
		text_at(16, y + PLAY_ASC, PLAY_PX, playing ? COL_TRACK : COL_FG,
			fit(PLAY_PX, buf, (int32_t)W - 40));
	}
	if (n_songs > list_vis()) {
		int32_t track = (int32_t)H - LIST_Y;
		int32_t bar = track * list_vis() / n_songs;
		int32_t off = track * scroll / n_songs;

		if (bar < 8) {
			bar = 8;
		}
		aster_ge_rect(&ge, W - 3, LIST_Y + off, 3, (uint32_t)bar, COL_ACC);
	}

	/* Failure toast, over the last row rather than beside it: it is
	 * answering the tap that is still under the finger, and a list that
	 * shifted to make room would move the next target. */
	if (err_frames > 0) {
		int32_t y = (int32_t)H - (PLAY_LH + 6);

		aster_ge_rect_r(&ge, 8, y, W - 16, (uint32_t)(PLAY_LH + 4), COL_ERR,
			      6, 0);
		text_at(16, y + PLAY_ASC, PLAY_PX, COL_FG,
			fit(PLAY_PX, err_line, (int32_t)W - 40));
	}
}

/* An eighth note, drawn from primitives: there is no image the app can load
 * through the sprite cache (that decoder takes BMP/PNG/JPEG, and the launcher
 * icon is MJXI), and no album art over the ABI anyway. */
static void draw_note(int32_t cx, int32_t cy, uint16_t col)
{
	aster_ge_ellipse(&ge, cx - 12, cy + 10, 15, 11, col, 0);
	aster_ge_rect(&ge, cx + 1, cy - 34, 5, 44, col);
	aster_ge_tri(&ge, cx + 6, cy - 34, cx + 6, cy - 16, cx + 24, cy - 26, col);
}

static void draw_now(const aster_audio_state_t *st)
{
	char buf[32];
	int32_t pct = 0;

	aster_ge_clear(&ge, COL_BG);
	draw_top(cur >= 0 ? names[cur] : "播放器");

	/* cover */
	aster_ge_rect_r(&ge, 20, 36, W - 40, 124, COL_PANEL, 16, 0);
	aster_ge_rect_r(&ge, 26, 42, W - 52, 112, COL_TRACK, 12, 0);
	draw_note((int32_t)W / 2 + 4, 100,
		  st->state == ASTER_AUDIO_PLAYING ? COL_ACC : COL_DIM);

	/* title */
	if (cur >= 0) {
		text_center((int32_t)W / 2, 182, PLAY_PX, COL_FG,
			    fit(PLAY_PX, names[cur], (int32_t)W - 20));
	} else {
		text_center((int32_t)W / 2, 182, PLAY_PX, COL_DIM, "未选择曲目");
	}

	/* progress */
	if (st->msec_total) {
		pct = (int32_t)((uint64_t)st->msec_done * 100u / st->msec_total);
		if (pct > 100) {
			pct = 100;
		}
	}
	aster_ge_rect_r(&ge, 12, PBAR_Y, W - 24, PBAR_H, COL_TRACK, PBAR_H / 2, 0);
	if (pct > 0) {
		int32_t fw = (int32_t)((W - 24) * (uint32_t)pct / 100u);

		if (fw < PBAR_H) {
			fw = PBAR_H;
		}
		aster_ge_rect_r(&ge, 12, PBAR_Y, (uint32_t)fw, PBAR_H, COL_ACC,
			      PBAR_H / 2, 0);
	}
	fmt_time(buf, sizeof(buf), st->msec_done);
	text_at(12, TIME_BASE, PLAY_PX, COL_DIM, buf);
	if (st->msec_total) {
		fmt_time(buf, sizeof(buf), st->msec_total);
		text_right((int32_t)W - 12, TIME_BASE, PLAY_PX, COL_DIM, buf);
	}

	/* transport: prev / play-pause / next */
	aster_ge_ellipse(&ge, 52, PB_CY, 22, 22, COL_PANEL, 0);
	aster_ge_tri(&ge, 60, PB_CY - 11, 60, PB_CY + 11, 42, PB_CY, COL_FG);
	aster_ge_ellipse(&ge, 188, PB_CY, 22, 22, COL_PANEL, 0);
	aster_ge_tri(&ge, 180, PB_CY - 11, 180, PB_CY + 11, 198, PB_CY, COL_FG);

	aster_ge_ellipse(&ge, PB_CX, PB_CY, PB_R, PB_R, COL_ACC, 0);
	if (st->state == ASTER_AUDIO_PLAYING) {
		aster_ge_rect(&ge, PB_CX - 11, PB_CY - 13, 8, 26, COL_TRACK);
		aster_ge_rect(&ge, PB_CX + 3, PB_CY - 13, 8, 26, COL_TRACK);
	} else {
		aster_ge_tri(&ge, PB_CX - 9, PB_CY - 14, PB_CX - 9, PB_CY + 14,
			   PB_CX + 14, PB_CY, COL_TRACK);
	}

	/* volume: tap anywhere on the band to set it */
	aster_ge_rect(&ge, 40, VOL_Y, W - 80, 6, COL_TRACK);
	if (st->volume) {
		aster_ge_rect(&ge, 40, VOL_Y,
			    (uint32_t)((W - 80) * st->volume / 100u), 6,
			    COL_ACC);
	}
	aster_ge_rect(&ge, 30, VOL_Y - 3, 6, 12, COL_DIM);
}

static void draw_lyrics(const aster_audio_state_t *st)
{
	int idx = lyric_at(st->msec_done);
	int first, i, vis;

	aster_ge_clear(&ge, COL_BG);
	draw_top(cur >= 0 ? names[cur] : "歌词");

	if (!lyr_n) {
		int32_t cy = (int32_t)H / 2;

		text_center((int32_t)W / 2, cy, PLAY_PX, COL_DIM,
			    aster_text_ready(PLAY_PX) ? "无歌词" : "字体未就绪");
		text_center((int32_t)W / 2, cy + PLAY_LH, PLAY_PX, COL_DIM,
			    aster_text_ready(PLAY_PX) ? "同名 .lrc 或 ID3 歌词"
						    : "插卡后重启设备");
		return;
	}
	/* How many lines fit between the top bar and the bottom of the panel.
	 * The first baseline is LYR_TOP + one step; after that each line adds
	 * a step, and the ink runs ~5 px below its baseline. */
	vis = ((int32_t)H - LYR_TOP - 5) / PLAY_LH;
	if (vis < 1) {
		vis = 0;	/* no room at all: draw the bar and nothing else */
	}

	/* keep the current line mid-screen */
	first = idx - vis / 2;
	if (first < 0) {
		first = 0;
	}
	if (first > lyr_n - vis) {
		first = lyr_n > vis ? lyr_n - vis : 0;
	}
	for (i = 0; i < vis && first + i < lyr_n; i++) {
		int k = first + i;
		uint32_t len = lyr_len[k];
		int32_t base = LYR_TOP + (i + 1) * PLAY_LH;

		memcpy(lyline, lyr + lyr_off[k], len);
		lyline[len] = 0;
		/* Every line draws at the one face; the old code measured to
		 * decide between 16 and 12 px. There is no second size to drop
		 * to, so a line too wide simply elides -- which is what the
		 * narrow branch was doing anyway, just after a needless
		 * re-measure at a size that no longer exists. */
		text_center((int32_t)W / 2, base, PLAY_PX,
			    k == idx ? COL_ACC : COL_DIM,
			    fit(PLAY_PX, lyline, (int32_t)W - 16));
	}
}

static int32_t draw(const aster_audio_state_t *st)
{
	if (page == 2) {
		draw_lyrics(st);
	} else if (page == 1) {
		draw_now(st);
	} else {
		draw_library();
	}
	return aster_ge_present(&ge);
}

/* ---------------- input ---------------- */

static void hit_transport(int32_t x, int32_t y)
{
	int32_t dx, dy;

	if (y < (int32_t)VOL_BAND) {
		dx = x - 52;
		dy = y - PB_CY;
		if (dx * dx + dy * dy <= 22 * 22) {
			skip(-1);
			return;
		}
		dx = x - 188;
		if (dx * dx + dy * dy <= 22 * 22) {
			skip(1);
			return;
		}
		dx = x - PB_CX;
		if (dx * dx + dy * dy <= PB_R * PB_R) {
			transport();
		}
		return;
	}
	/* volume band */
	{
		int32_t v = (x - 40) * 100 / (int32_t)(W - 80);

		if (v < 0) {
			v = 0;
		}
		if (v > 100) {
			v = 100;
		}
		aster_audio_set_volume(v);
		dirty = 1;
	}
}

/* Keyboard (evdev codes, as forwarded by the ABI). Gives a board with a
 * keypad a way through every screen, and gives tests a way in that does not
 * need a finger: `hw key inject <code> down|up`.
 *
 * The page cycle matters: the two header arrows are touch-only, so without
 * a key for it a device with no digitiser could never reach the transport
 * or lyric views at all. */
#define K_ESC   1u	/* back: lyrics -> now -> library           */
#define K_MINUS 12u	/* volume down, from any page               */
#define K_EQUAL 13u	/* volume up                                */
#define K_ENTER 28u
#define K_SPACE 57u	/* forward: library -> now -> lyrics -> ...  */
#define K_UP    103u
#define K_LEFT  105u
#define K_RIGHT 106u
#define K_DOWN  108u

static void set_volume(int delta)
{
	int32_t v = aster_audio_get_volume();

	if (v < 0) {
		return;
	}
	v += delta;
	if (v < 0) {
		v = 0;
	}
	if (v > 100) {
		v = 100;
	}
	aster_audio_set_volume(v);
	dirty = 1;
}

static void on_key(const aster_event_t *ev)
{
	uint32_t code = ev->u.key.code;

	if (!ev->u.key.pressed) {
		return;
	}
	switch (code) {
	case K_ESC:
		page = page == 2 ? 1 : 0;
		dirty = 1;
		break;
	case K_SPACE:
		page = page == 2 ? 0 : page + 1;
		dirty = 1;
		break;
	case K_ENTER:
		if (page == 0) {
			play(sel, 1);
		} else {
			transport();	/* ENTER is the main verb of every page */
		}
		break;
	case K_LEFT:
		skip(-1);
		break;
	case K_RIGHT:
		skip(1);
		break;
	case K_MINUS:
	case K_EQUAL:
		set_volume(code == K_EQUAL ? 5 : -5);
		break;
	case K_UP:
	case K_DOWN:
		if (page == 0) {
			sel += (code == K_DOWN) ? 1 : -1;
			sel_follow();
			dirty = 1;
		} else if (page == 1) {
			set_volume(code == K_DOWN ? -5 : 5);
		}
		break;
	default:
		break;
	}
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
	if (ev->u.touch.action != ASTER_TOUCH_UP || down_x < 0) {
		return;
	}
	{
		int32_t dx = x - down_x;
		int32_t dy = y - down_y;
		int32_t adx = dx < 0 ? -dx : dx;
		int32_t ady = dy < 0 ? -dy : dy;

		down_x = down_y = -1;
		if (y < TOP_H && x < 34) {	/* back */
			page = page == 2 ? 1 : 0;
			dirty = 1;
			return;
		}
		if (y < TOP_H && x > (int32_t)W - 48) {
			page = page == 2 ? 1 : 2;
			dirty = 1;
			return;
		}
		if (page == 0) {
			if (adx > 32 || ady > 32) {
				if (ady > adx) {
					int32_t maxs = n_songs > list_vis()
							      ? n_songs - list_vis()
							      : 0;
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
			if (y >= LIST_Y) {
				int i = (int32_t)((y - LIST_Y) / ROW_H);

				if (i >= 0 && i < list_vis() &&
				    scroll + i < n_songs) {
					play(scroll + i, 1);
					dirty = 1;
				}
			}
			return;
		}
		if (page == 1) {
			if (adx > 32 || ady > 32) {
				if (adx >= ady) {
					skip(dx < 0 ? 1 : -1);
				}
				return;
			}
			hit_transport(x, y);
		}
	}
}

/* ---------------- lifecycle ---------------- */

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
		aster_logf(ASTER_LOG_ERROR, "player surface: %d", err);
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

	scan_all();
	aster_logf(ASTER_LOG_INFO, "player %ux%u %d tracks in %s canvas=%d", W, H,
		 n_songs, root,
		 (buf.flags & ASTER_SURFACE_F_PANEL_CANVAS) != 0u);
	return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_stop(void)
{
	/* Leaving the app must not leave audio running with no way to reach
	 * the transport. */
	aster_audio_stop();
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
	}
	if (ev->type != ASTER_EVENT_FRAME) {
		return;
	}

	{
		aster_audio_state_t st = { .size = sizeof(st), .version = 2 };
		int32_t sec, idx;

		/* The toast comes down on its own clock, so the tick that
		 * retires it is also the only redraw it needs. */
		if (err_frames > 0 && --err_frames == 0) {
			dirty = 1;
		}
		if (aster_audio_get_state(&st, sizeof(st)) != ASTER_OK) {
			if (dirty) {
				memset(&st, 0, sizeof(st));
				dirty = 0;
				draw(&st);
			}
			return;
		}
		if (st.state == ASTER_AUDIO_PLAYING) {
			saw_playing = 1;
			fetching = 0;
			last_ms = (int32_t)st.msec_done;
			last_total = (int32_t)st.msec_total;
		}
		/* The end of a track lands in IDLE -- and so does a file the
		 * decoder will not read, from ~0 ms. Remembering where the
		 * last PLAYING report stood is what tells them apart; by the
		 * time we are told IDLE the firmware has zeroed the position.
		 * Advancing on both would walk the whole library at speed the
		 * moment one file in it is unreadable, so a short stop gets
		 * the error toast instead. */
		if (st.state == ASTER_AUDIO_IDLE && saw_playing && !fetching) {
			saw_playing = 0;
			if (last_total > 0 && last_ms + 1500 >= last_total) {
				if (n_songs > 1) {
					skip(1);
				} else {
					dirty = 1;
				}
			} else {
				if (cur >= 0) {
					aster_snprintf(err_line, sizeof(err_line),
						     "无法播放 %s", names[cur]);
					err_frames = 60;
				}
				dirty = 1;
			}
			return;
		}

		sec = (int32_t)(st.msec_done / 1000u);
		idx = page == 2 ? lyric_at(st.msec_done) : -1;

		/* aster_gfx_fonts_pending: baked bitmaps land asynchronously, so a
		 * screen whose own state is unchanged may still be showing blank
		 * boxes -- see the ABI note on the call. */
		if (st.state != seen_state || st.volume != (uint32_t)seen_vol ||
		    sec != seen_sec || idx != seen_lyr || dirty ||
		    aster_gfx_fonts_pending()) {
			seen_state = st.state;
			seen_vol = (int32_t)st.volume;
			seen_sec = sec;
			seen_lyr = idx;
			dirty = 0;
			if (page == 1 || page == 2) {
				fetch_lyrics();
			}
			draw(&st);
		}
	}
}
