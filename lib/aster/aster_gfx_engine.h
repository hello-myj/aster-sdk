/* SPDX-License-Identifier: MIT
 * aster_gfx_engine.h - command-ring layer on top of aster_gfx.h (ABI 2.2).
 *
 * Cheap geometry encodes as 24-byte commands into a static ring; a whole
 * frame goes out in ONE aster_gfx_exec() call and the firmware rasterizes
 * into the same surface buffer aster_gfx_t points at, so v1 inline pixel
 * loops and engine commands freely mix (just keep the dirty-rect rules in
 * mind: engine commands auto-track bounds; inline writes need the explicit
 * rect form of present, or your own aster_surface_flush()).
 *
 * Typical frame:
 *   static aster_ge_t ge;
 *   aster_ge_init(&ge, surf, ring, arena);           // once at start
 *   aster_ge_clear(&ge, BLACK);
 *   aster_ge_text(&ge, 8, 24, 16, WHITE, 0, "得分 42");
 *   ...                                            // any # of commands
 *   aster_ge_present(&ge);                           // exec + dirty push
 *
 * Ring-full mid-frame auto-spills (partial present keeps frame coherence);
 * the buffer is scratch and resets on every present.
 */
#ifndef ASTER_GFX_ENGINE_H
#define ASTER_GFX_ENGINE_H

#include <stddef.h>  /* builtin freestanding header */
#include <stdint.h>
#include <stdbool.h>

#include <aster/gfx.h>
#include "aster_gfx.h"
/* For the face metrics behind aster_ge_text's dirty box. aster_text.h is built on
 * the aster/gfx.h ABI header, not on this one, so this is not a cycle. */
#include "aster_text.h"

/* memcpy/memset/strlen come from the SDK libc (aster_libc.h) that every app
 * pulls in via aster_imports.h; declare them here too so the header compiles
 * standalone under -nostdlib without clashing (identical prototypes). */
#ifndef ASTER_LIBC_H
void  *memcpy(void *dst, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);
size_t strlen(const char *s);
#endif

typedef struct {
	aster_handle_t surf;
	uint32_t w, h;		/* surface size (for auto dirty clamp) */
	aster_gfx_cmd_t *ring;
	uint32_t ring_n;	/* capacity, commands */
	uint32_t n;		/* queued */
	uint8_t *arena;
	uint32_t arena_cap, arena_used;
	uint32_t dx0, dy0, dx1, dy1;	/* accumulated bounds */
	bool dirty;
} aster_ge_t;

static inline void aster_ge_init(aster_ge_t *g, aster_handle_t surf, uint32_t w,
			       uint32_t h, aster_gfx_cmd_t *ring, uint32_t ring_n,
			       uint8_t *arena, uint32_t arena_cap)
{
	memset(g, 0, sizeof(*g));
	g->surf = surf;
	g->w = w;
	g->h = h;
	g->ring = ring;
	g->ring_n = ring_n;
	g->arena = arena;
	g->arena_cap = arena_cap;
}

/* Push the auto-dirty box through the engine as an explicit present rect
 * (call after hand-painting with the v1 inline fns). */
static inline int32_t aster_ge_touch(aster_ge_t *g, uint32_t x, uint32_t y,
				   uint32_t w, uint32_t h)
{
	uint32_t x1 = x + w, y1 = y + h;

	if (!g->dirty || x < g->dx0) {
		g->dx0 = x;
	}
	if (!g->dirty || y < g->dy0) {
		g->dy0 = y;
	}
	if (x1 > g->dx1) {
		g->dx1 = x1;
	}
	if (y1 > g->dy1) {
		g->dy1 = y1;
	}
	g->dirty = true;
	return 0;
}

static inline int32_t aster_ge_exec(aster_ge_t *g, uint32_t px, uint32_t py,
				  uint32_t pw, uint32_t ph)
{
	int32_t r = aster_gfx_exec(g->surf, g->ring, g->n, g->arena,
				 g->arena_used, px, py, pw, ph);

	g->n = 0;
	g->arena_used = 0;
	g->dirty = false;
	return r;
}

static inline aster_gfx_cmd_t *aster_ge_room(aster_ge_t *g)
{
	if (g->n == g->ring_n) {
		/* spill: push what accumulated so far, keep frame coherence */
		aster_ge_exec(g, 0, 0, 0, 0);
	}
	return &g->ring[g->n++];
}

static inline uint32_t aster_ge_arena(aster_ge_t *g, const void *p, uint32_t len)
{
	if (g->arena_used + len > g->arena_cap) {
		aster_ge_exec(g, 0, 0, 0, 0);
		if (len > g->arena_cap) {
			return 0xFFFFFFFFu;	/* too big for the arena */
		}
	}
	uint32_t off = g->arena_used;

	memcpy(g->arena + off, p, len);
	g->arena_used += len;
	return off;
}

/* ---- encoders (each <= a few stores; no firmware round-trip) ---- */

static inline void aster_ge_clear(aster_ge_t *g, uint16_t color)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_CLEAR;
	c->w[1] = color;
	aster_ge_touch(g, 0, 0, g->w, g->h);
}

static inline void aster_ge_rect(aster_ge_t *g, int32_t x, int32_t y, uint32_t w,
			       uint32_t h, uint16_t color)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_RECT_F;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x, (uint16_t)y);
	c->w[2] = ASTER_GFX_PACK(w, h);
	c->w[3] = color;
	aster_ge_touch(g, (uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
		     w, h);
}

/* radius r corners; outline=1 draws only the rounded border */
static inline void aster_ge_rect_r(aster_ge_t *g, int32_t x, int32_t y, uint32_t w,
				 uint32_t h, uint16_t color, uint16_t r,
				 uint16_t outline)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_RECT_S;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x, (uint16_t)y);
	c->w[2] = ASTER_GFX_PACK(w, h);
	c->w[3] = color;
	c->w[4] = ASTER_GFX_PACK(r, outline);
	aster_ge_touch(g, (uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
		     w, h);
}

static inline void aster_ge_line_aa(aster_ge_t *g, int32_t x0, int32_t y0,
				  int32_t x1, int32_t y1, uint16_t color)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_LINE_AA;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x0, (uint16_t)y0);
	c->w[2] = ASTER_GFX_PACK((uint16_t)x1, (uint16_t)y1);
	c->w[3] = color;
	{
		int32_t mx = x0 < x1 ? x0 : x1, my = y0 < y1 ? y0 : y1;
		int32_t xx = x0 < x1 ? x1 : x0, yy = y0 < y1 ? y1 : y0;

		aster_ge_touch(g, (uint32_t)(mx > 0 ? mx : 0),
			     (uint32_t)(my > 0 ? my : 0),
			     (uint32_t)(xx - mx + 1), (uint32_t)(yy - my + 1));
	}
}

/* filled (or outlined, stroke=1) ellipse centred cx/cy with radii rx/ry */
static inline void aster_ge_ellipse(aster_ge_t *g, int32_t cx, int32_t cy,
				  uint32_t rx, uint32_t ry, uint16_t color,
				  uint16_t stroke)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_ELLIPSE;
	c->w[1] = ASTER_GFX_PACK((uint16_t)cx, (uint16_t)cy);
	c->w[2] = ASTER_GFX_PACK(rx, ry);
	c->w[3] = ASTER_GFX_PACK(color, stroke);
	aster_ge_touch(g, (uint32_t)(cx - (int32_t)rx > 0 ? cx - (int32_t)rx : 0),
		     (uint32_t)(cy - (int32_t)ry > 0 ? cy - (int32_t)ry : 0),
		     2u * rx + 1u, 2u * ry + 1u);
}

static inline void aster_ge_tri(aster_ge_t *g, int32_t x0, int32_t y0, int32_t x1,
			      int32_t y1, int32_t x2, int32_t y2,
			      uint16_t color)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);
	int32_t mx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
	int32_t my = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
	int32_t xx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
	int32_t yy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);

	c->w[0] = ASTER_GFX_TRI;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x0, (uint16_t)y0);
	c->w[2] = ASTER_GFX_PACK((uint16_t)x1, (uint16_t)y1);
	c->w[3] = ASTER_GFX_PACK((uint16_t)x2, (uint16_t)y2);
	c->w[4] = color;
	aster_ge_touch(g, (uint32_t)(mx > 0 ? mx : 0), (uint32_t)(my > 0 ? my : 0),
		     (uint32_t)(xx - mx + 1), (uint32_t)(yy - my + 1));
}

static inline void aster_ge_grad(aster_ge_t *g, int32_t x, int32_t y, uint32_t w,
			       uint32_t h, uint16_t c0, uint16_t c1,
			       uint16_t horizontal)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_GRAD;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x, (uint16_t)y);
	c->w[2] = ASTER_GFX_PACK(w, h);
	c->w[3] = c0;
	c->w[4] = c1;
	c->w[5] = horizontal;
	aster_ge_touch(g, (uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
		     w, h);
}

/* baseline text. bg != 0xFFFF paints an RGB565 backdrop behind glyphs. */
#define ASTER_GFX_NOBG 0xFFFFu
static inline int32_t aster_ge_text(aster_ge_t *g, int32_t x, int32_t baseline_y,
				  uint32_t px, uint16_t fg, uint16_t bg,
				  const char *utf8)
{
	uint32_t len = (uint32_t)strlen(utf8);
	uint32_t off = aster_ge_arena(g, utf8, len);

	if (off == 0xFFFFFFFFu) {
		return ASTER_EINVAL;
	}
	aster_gfx_cmd_t *c = aster_ge_room(g);

	c->w[0] = ASTER_GFX_TEXT;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x, (uint16_t)baseline_y);
	c->w[2] = ASTER_GFX_PACK(px & 0xFFFFu, fg);
	c->w[3] = off;
	c->w[4] = len;
	c->w[5] = (bg == ASTER_GFX_NOBG) ? 0u : (0x10000u | (uint32_t)bg);
	/* Dirty bounds. Both used to under-cover, and under-covering is the
	 * direction that strands stale pixels on the glass: aster_ge_present
	 * clamps the box to the surface, so over-covering is free.
	 *
	 * Height was `px + 4` from `baseline_y - px - 2`, which stops 3 px
	 * short of a descender -- the ascent at the baked face is 21 for a
	 * 20 px size, so it even exceeds px, and the ink runs a further
	 * (line step - ascent) below the baseline. Both come from the face.
	 *
	 * Width was `len * (px/2 + 1)`, commented "worst case for CJK-heavy
	 * runs". CJK is 3 bytes per px-wide glyph; Latin is 1 byte per ~0.64*px
	 * glyph, so per byte Latin is the worst case and that bound never held
	 * it. `len * px` does hold: no glyph at size px advances more than px,
	 * and no glyph is shorter than one byte. */
	int32_t asc = aster_text_asc(px);

	aster_ge_touch(g, (uint32_t)(x < 0 ? 0 : x),
		     (uint32_t)(baseline_y > asc ? baseline_y - asc : 0),
		     (uint32_t)len * px, (uint32_t)aster_text_line_h(px));
	return 0;
}

static inline void aster_ge_bmp(aster_ge_t *g, uint32_t handle, uint32_t sw,
			      uint32_t sh, int32_t x, int32_t y, uint16_t scale256,
			      uint16_t alpha)
{
	aster_gfx_cmd_t *c = aster_ge_room(g);
	uint32_t op = ASTER_GFX_BMP;

	if (scale256 != 256u && alpha != 255u) {
		op = ASTER_GFX_BMP_SA;
	} else if (alpha != 255u) {
		op = ASTER_GFX_BMP_A;
	} else if (scale256 != 256u) {
		op = ASTER_GFX_BMP_S;
	}
	c->w[0] = op;
	c->w[1] = ASTER_GFX_PACK((uint16_t)x, (uint16_t)y);
	c->w[2] = handle;
	c->w[4] = (op == ASTER_GFX_BMP_S || op == ASTER_GFX_BMP_SA) ? scale256 : alpha;
	if (op == ASTER_GFX_BMP_SA) {
		c->w[5] = alpha;
	}
	{
		uint32_t ww = scale256 ? (sw * scale256 / 256u) : sw;
		uint32_t hh2 = scale256 ? (sh * scale256 / 256u) : sh;

		aster_ge_touch(g, (uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
			     ww, hh2);
	}
}

/* Submit + push accumulated dirty region (the common frame end). */
static inline int32_t aster_ge_present(aster_ge_t *g)
{
	if (!g->dirty && g->n == 0) {
		return 0;
	}
	uint32_t x0 = g->dirty ? g->dx0 : 0, y0 = g->dirty ? g->dy0 : 0;
	uint32_t x1 = g->dirty ? g->dx1 : g->w, y1 = g->dirty ? g->dy1 : g->h;

	if (x1 > g->w) {
		x1 = g->w;
	}
	if (y1 > g->h) {
		y1 = g->h;
	}
	if (x0 >= x1 || y0 >= y1) {
		x0 = y0 = 0;
		x1 = g->w;
		y1 = g->h;
	}
	return aster_ge_exec(g, x0, y0, x1 - x0, y1 - y0);
}

/* Convenience ring storage for one 240x320 game surface. */
#define ASTER_GE_RING_DEFAULT 96u
#define ASTER_GE_ARENA_DEFAULT 768u
static inline uint32_t aster_ge_default_ring(aster_gfx_cmd_t **ring, uint8_t **arena)
{
	static aster_gfx_cmd_t r[ASTER_GE_RING_DEFAULT];
	static uint8_t a[ASTER_GE_ARENA_DEFAULT];

	*ring = r;
	*arena = a;
	return ASTER_GE_RING_DEFAULT;
}

#endif /* ASTER_GFX_ENGINE_H */
