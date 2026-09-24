/* SPDX-License-Identifier: MIT
 * aster_text.h - cpfont metrics + UTF-8 line breaking for guest apps.
 *
 * WHY THIS EXISTS
 * The ABI has no font-metric query. ASTER_GFX_TEXT takes a baseline and clamps
 * `px` to the nearest baked face; an app that lays out more than one line
 * needs that face's line height and ascent, and there is nowhere to ask for
 * them. So they are written down here. These are not estimates -- they are
 * the numbers build_cpfont.py wrote into the .cpfont headers (advY at byte
 * 44, asc at 45), and they are what the rasterizer's pen actually advances.
 *
 * ONE FACE. The bake holds a single 20 px face (the table below), so `px` is
 * a hint rather than a choice: 12, 14 and 16 all draw at 20. Layouts written
 * against a 12 px advance have to be re-derived rather than
 * searched-and-replaced -- the text gets wider, and a box that was
 * comfortable at 12 will clip. The two numbers that matter for that are a
 * row step of 26 and an ascent of 21.
 *
 * What that buys: eight resident metric tables used to sit in PSRAM with no
 * release path at all. One face returns 1.73 MB of it.
 *
 * Font metrics come from <aster/font_metrics.h> in the pinned SDK contract.
 * Upgrade them through tools/import_contract.py with a released bundle.
 * This library never reads firmware sources or font build outputs.
 *
 * MEASURING
 * aster_gfx_text_w() sums per-glyph advances and returns the WIDEST line when
 * the run contains '\n' (firmware: aster_gfx_text_measure). Advances are
 * per-glyph with no kerning, so summing per-character calls is exactly equal
 * to one call over the whole run -- which is what aster_text_measure does, one
 * character at a time, so the wrap below never has to guess.
 *
 * Apps: sdk/apps/reader (body text), sdk/apps/player (lyrics). Both also
 * depend on the cpfont faces being READY; aster_text_ready() is the check that
 * turns "font not streamed yet" into a message instead of a blank page.
 */
#ifndef ASTER_TEXT_H
#define ASTER_TEXT_H

#include <stdint.h>

#include <aster/gfx.h>

#include "aster_gfx.h"

#ifndef ASTER_LIBC_H
void  *memcpy(void *dst, const void *src, unsigned long n);
void  *memset(void *s, int c, unsigned long n);
unsigned long strlen(const char *s);
#endif

/* The baked faces, in the order build_cpfont.py emits them. line_h is advY
 * (one baseline step); asc is the ascent, i.e. the first baseline of a block,
 * so row 0 is the top of the first line's ink. `asc` values are positive; the
 * .cpfont stores the same magnitude. */
typedef struct {
	uint8_t px;
	uint8_t line_h;
	uint8_t asc;
} aster_text_face_t;

#include <aster/font_metrics.h>
static const aster_text_face_t ASTER_TEXT_FACES[] = ASTER_FONT_FACE_METRICS;
#define ASTER_TEXT_FACE_N ASTER_FONT_BAKED_N

/* Nearest baked size, ALWAYS -- there is no "no face" answer any more.
 *
 * The old version returned -1 outside a +/-4 px window, which was right when
 * a rasterizer sat behind the table: an unbaked size still drew, from
 * tiny_ttf, and 0 (meaning "no metrics known") was a truthful answer. With
 * one face in the bake that window is 16..24, so 12 and 14 -- which every
 * app in the tree asks for by name -- returned -1, and -1 made
 * aster_text_line_h()/aster_text_asc() return 0. Callers use those as a row
 * advance: the reader multiplied by a zero line height and the player's
 * lyric column collapsed onto one baseline. A silent zero is a far worse
 * failure than a wrong-but-plausible metric, and the firmware clamps to the
 * nearest face unconditionally, so this now mirrors it exactly.
 *
 * The consequence to keep in mind: a size that is not baked draws at the
 * nearest baked size, so a layout computed for 12 px text will overflow when
 * the face is 20 px. It reads rather than vanishes -- which is the failure
 * mode to want, but it is a silent one. */
static inline int aster_text_snap(int px)
{
	int best = -1, pick = ASTER_TEXT_FACES[0].px;
	uint32_t i;

	for (i = 0; i < ASTER_TEXT_FACE_N; i++) {
		int d = (int)ASTER_TEXT_FACES[i].px - px;

		if (d < 0) {
			d = -d;
		}
		if (best < 0 || d < best) {
			best = d;
			pick = (int)ASTER_TEXT_FACES[i].px;
		}
	}
	return pick;
}

static inline const aster_text_face_t *aster_text_face(int px)
{
	int snap = aster_text_snap(px);
	uint32_t i;

	for (i = 0; i < ASTER_TEXT_FACE_N; i++) {
		if ((int)ASTER_TEXT_FACES[i].px == snap) {
			return &ASTER_TEXT_FACES[i];
		}
	}
	return &ASTER_TEXT_FACES[0];
}

/* Baseline step. Never 0: see aster_text_snap. */
static inline int32_t aster_text_line_h(int px)
{
	return (int32_t)aster_text_face(px)->line_h;
}

/* Baseline of the first line of a block whose top edge is `y`. */
static inline int32_t aster_text_asc(int px)
{
	return (int32_t)aster_text_face(px)->asc;
}

/* ---- UTF-8 ---- */

/* Bytes in the sequence starting at s[0]; 1 for ASCII and for a stray
 * continuation byte, so a walk always makes progress on corrupt input. */
static inline uint32_t aster_utf8_len(const char *s)
{
	uint8_t c = (uint8_t)s[0];

	if (c < 0x80u) {
		return 1u;
	}
	if ((c & 0xE0u) == 0xC0u) {
		return 2u;
	}
	if ((c & 0xF0u) == 0xE0u) {
		return 3u;
	}
	if ((c & 0xF8u) == 0xF0u) {
		return 4u;
	}
	return 1u;
}

/* Decode one code point from s[0..len). Truncated tails decode as far as
 * they go rather than reading past the slice. */
static inline uint32_t aster_utf8_cp(const char *s, uint32_t len)
{
	uint32_t k = aster_utf8_len(s);
	uint32_t cp, i;

	if (k > len) {
		k = len;
	}
	if (k <= 1u) {
		return (uint8_t)s[0];
	}
	cp = (uint32_t)((uint8_t)s[0] & (0xFFu >> (k + 1u)));
	for (i = 1u; i < k; i++) {
		cp = (cp << 6) | ((uint32_t)(uint8_t)s[i] & 0x3Fu);
	}
	return cp;
}

/* Advance width of a UTF-8 run at `px`, summed per character. Negative on a
 * firmware error. A run containing '\n' measures only the widest segment --
 * callers that care about total width must not feed it newlines. */
static inline int32_t aster_text_measure(uint32_t px, const char *s, uint32_t len)
{
	uint32_t i = 0;
	int32_t w = 0;

	while (i < len) {
		uint32_t k = aster_utf8_len(s + i);
		int32_t a;

		if (i + k > len) {
			k = len - i;
		}
		a = aster_gfx_text_w(px, s + i, k);
		if (a < 0) {
			return a;
		}
		w += a;
		i += k;
	}
	return w;
}

/* A non-empty run that measures zero wide means the cpfont faces are not
 * resident (SD missing, still streaming): every glyph would draw nothing.
 * The rasterizer never fails in that state, it just goes blank -- so ask. */
static inline int aster_text_ready(uint32_t px)
{
	return aster_text_measure(px, "M", 1u) > 0;
}

/* ---- line breaking ---- */

/* Break opportunity before a code point.
 *   NONE  - must not start a line here (Latin word interiors, CJK closers)
 *   PLAIN - may start a line here (CJK ideographs, kana, fullwidth forms)
 *   SPACE - may start here, and the space run itself is dropped */
#define ASTER_TB_NONE  0u
#define ASTER_TB_PLAIN 1u
#define ASTER_TB_SPACE 2u

static inline uint32_t aster_text_brk(uint32_t cp)
{
	if (cp == (uint32_t)' ' || cp == (uint32_t)'\t' || cp == 0x3000u) {
		return ASTER_TB_SPACE;
	}
	if (cp < 0x2E80u) {
		return ASTER_TB_NONE;	/* ASCII/Latin/Greek: keep words whole */
	}
	switch (cp) {			/* CJK closers: never start a line */
	case 0x3001u: case 0x3002u: case 0x300Bu: case 0x300Du: case 0x3011u:
	case 0x3015u: case 0xFF01u: case 0xFF09u: case 0xFF0Cu: case 0xFF0Eu:
	case 0xFF1Bu: case 0xFF1Du: case 0xFF1Fu: case 0x2019u: case 0x201Du:
	case 0x2026u: case 0x2014u: case 0x00B7u: case 0x203Au:
		return ASTER_TB_NONE;
	default:
		return ASTER_TB_PLAIN;
	}
}

/* Opener: a break right after it is not allowed either. */
static inline int aster_text_no_end(uint32_t cp)
{
	switch (cp) {
	case 0x300Cu: case 0x300Eu: case 0x3010u: case 0x3014u: case 0xFF08u:
	case 0xFF3Bu: case 0x2018u: case 0x201Cu:
		return 1;
	default:
		return 0;
	}
}

static inline uint32_t aster_text_skip_sp(const char *s, uint32_t i, uint32_t len)
{
	while (i < len && (s[i] == ' ' || s[i] == '\t')) {
		i++;
	}
	return i;
}

/* Wrap ONE visual line out of src[0..len) against `maxw` pixels.
 *
 * Returns the offset to resume at; *out_len gets the line's byte length.
 * `*out_len == 0` with a non-zero return means a blank line (a bare '\n'),
 * which is a line to render empty -- not the end of input. The caller's end
 * condition is its own position reaching the end of the *file*, never this
 * return value alone.
 *
 * Break preference: the last space, else the last CJK boundary, else a hard
 * break mid-run (only reachable for a single run wider than the line, e.g. a
 * URL -- or a Latin word on a 12 px face in a 200 px column).
 *
 * Never splits a UTF-8 sequence: offsets only ever land on a lead byte. */
static inline uint32_t aster_text_line(uint32_t px, const char *src, uint32_t len,
				     int32_t maxw, uint32_t *out_len)
{
	uint32_t i = 0, brk = 0, brk_kind = ASTER_TB_NONE;
	int32_t w = 0;
	int no_end = 0;

	while (i < len && src[i] != '\n') {
		uint32_t k = aster_utf8_len(src + i);
		uint32_t cp;
		int32_t aw;

		if (i + k > len) {
			k = len - i;
		}
		cp = aster_utf8_cp(src + i, k);
		/* A break is recorded only at a position already known to fit
		 * (w <= maxw here), so taking it can never overflow. */
		if (i > 0 && !no_end) {
			uint32_t b = aster_text_brk(cp);

			if (b != ASTER_TB_NONE) {
				brk = i;
				brk_kind = b;
			}
		}
		aw = aster_gfx_text_w(px, src + i, k);
		if (aw < 0) {
			*out_len = i;
			return i;
		}
		if (i > 0 && w + aw > maxw) {
			if (brk > 0) {
				*out_len = brk;
				return brk_kind == ASTER_TB_SPACE
					       ? aster_text_skip_sp(src, brk, len)
					       : brk;
			}
			*out_len = i;
			return i;
		}
		w += aw;
		no_end = aster_text_no_end(cp);
		i += k;
	}
	*out_len = i;
	return i < len ? i + 1u : len;	/* eat the '\n' */
}

/* The truncation marker is three ASCII full stops, NOT U+2026. The bake
 * cannot supply the single-glyph form: its source font (ui.ttf) has no
 * U+2026 in its cmap at all, so it is absent from every face this project
 * can bake, and at px < big_from the bake takes the whole cmap -- there is
 * no subset rule to add it to. It is also undetectable from here: the
 * firmware reports a phantom 15 px advance for that codepoint while
 * painting nothing, so `aster_gfx_text_w(...) != 0` is not a liveness test.
 * The three dots are three mapped U+002E, so they measure and paint for
 * real, and they are what the layout can actually rely on. */
#define ASTER_TEXT_ELLIPSIS "..."
#define ASTER_TEXT_ELLIPSIS_N 3u

/* Copy as much of `src` as fits `maxw`, whole characters only, appending the
 * marker when something was dropped. Returns the bytes written (excl. NUL),
 * or -1 when `maxw` cannot hold even the marker (or `dst_max` cannot hold the
 * result). `dst` is NUL-terminated on both paths -- on -1 it is empty, never
 * a partial copy.
 *
 * The marker's width is RESERVED while filling rather than appended after the
 * fact. The old shape appended first and gave up when it no longer fit, which
 * returned -1 for exactly the strings eliding exists for -- and every caller's
 * -1 path draws the *unedited* string, so the fallback overflowed worse than
 * no elide at all. (Measured on the glass: a 26-byte SD directory name whose
 * elided form is 198 px wide came back -1, and the reader drew all 283 px of
 * it, running off the panel and under the scrollbar.) Reserving instead stops
 * the loop one character earlier, and the marker always lands inside `maxw`.
 *
 * The one case that must NOT be elided is a run that already fits: reserving
 * would cut it short and mark text that lost nothing. `src` is measured whole
 * first for that. It is a single call, and it is the same per-character sum
 * the loop below computes -- unless the run contains '\n', where the firmware
 * reports the widest LINE rather than the total. Callers pass single-line runs
 * (file names, titles, one wrapped lyric line), so the two agree. */
static inline int32_t aster_text_elide(uint32_t px, const char *src, uint32_t len,
				     int32_t maxw, char *dst, uint32_t dst_max)
{
	int32_t ell_w = aster_gfx_text_w(px, ASTER_TEXT_ELLIPSIS,
				       ASTER_TEXT_ELLIPSIS_N);
	int32_t full;
	uint32_t i = 0, o = 0;
	int32_t w = 0;

	if (dst_max == 0u) {
		return -1;
	}
	full = aster_gfx_text_w(px, src, len);
	if (full >= 0 && full <= maxw) {
		if (len + 1u > dst_max) {
			dst[0] = 0;
			return -1;
		}
		memcpy(dst, src, len);
		dst[len] = 0;
		return (int32_t)len;
	}
	if (ell_w < 0) {
		ell_w = 0;
	}
	if (ell_w > maxw) {
		dst[0] = 0;
		return -1;
	}
	while (i < len) {
		uint32_t k = aster_utf8_len(src + i);
		int32_t aw;

		if (i + k > len) {
			k = len - i;
		}
		aw = aster_gfx_text_w(px, src + i, k);
		if (aw < 0) {
			break;
		}
		if (w + aw + ell_w > maxw ||
		    o + k + ASTER_TEXT_ELLIPSIS_N >= dst_max) {
			break;
		}
		memcpy(dst + o, src + i, k);
		o += k;
		w += aw;
		i += k;
	}
	if (i < len) {			/* truncated: mark it */
		if (o + ASTER_TEXT_ELLIPSIS_N >= dst_max) {
			dst[0] = 0;
			return -1;
		}
		memcpy(dst + o, ASTER_TEXT_ELLIPSIS, ASTER_TEXT_ELLIPSIS_N);
		o += ASTER_TEXT_ELLIPSIS_N;
	}
	dst[o] = 0;
	return (int32_t)o;
}

#endif /* ASTER_TEXT_H */
