/* SPDX-License-Identifier: MIT
 * aster_gfx.h - header-only RGB565 drawing helpers for ASTER apps.
 *
 * Works on the surface back-buffer returned by aster_surface_acquire().
 * The buffer is treated as 16-bit little-endian RGB565 (ASTER_PIXEL_RGB565).
 */
#ifndef ASTER_GFX_H
#define ASTER_GFX_H

#include <stdint.h>

typedef struct {
	uint16_t *px;
	uint32_t w;
	uint32_t h;
	uint32_t pitch; /* bytes per row */
} aster_gfx_t;

#define ASTER_RGB565(r, g, b) \
	((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define ASTER_BLACK   ASTER_RGB565(0, 0, 0)
#define ASTER_WHITE   ASTER_RGB565(255, 255, 255)
#define ASTER_RED     ASTER_RGB565(255, 0, 0)
#define ASTER_GREEN   ASTER_RGB565(0, 255, 0)
#define ASTER_BLUE    ASTER_RGB565(0, 0, 255)
#define ASTER_YELLOW  ASTER_RGB565(255, 255, 0)
#define ASTER_CYAN    ASTER_RGB565(0, 255, 255)
#define ASTER_MAGENTA ASTER_RGB565(255, 0, 255)

static inline void aster_gfx_init(aster_gfx_t *g, void *buf, uint32_t w, uint32_t h,
				uint32_t pitch)
{
	g->px = (uint16_t *)buf;
	g->w = w;
	g->h = h;
	g->pitch = pitch;
}

static inline void aster_gfx_pixel(aster_gfx_t *g, uint32_t x, uint32_t y, uint16_t c)
{
	if (x < g->w && y < g->h) {
		g->px[(y * g->pitch / 2) + x] = c;
	}
}

static inline void aster_gfx_clear(aster_gfx_t *g, uint16_t c)
{
	for (uint32_t y = 0; y < g->h; y++) {
		uint16_t *row = &g->px[y * g->pitch / 2];
		for (uint32_t x = 0; x < g->w; x++) {
			row[x] = c;
		}
	}
}

static inline void aster_gfx_fill_rect(aster_gfx_t *g, uint32_t x, uint32_t y, uint32_t w,
				     uint32_t h, uint16_t c)
{
	uint32_t xe = x + w > g->w ? g->w : x + w;
	uint32_t ye = y + h > g->h ? g->h : y + h;
	for (uint32_t yy = y; yy < ye; yy++) {
		uint16_t *row = &g->px[yy * g->pitch / 2];
		for (uint32_t xx = x; xx < xe; xx++) {
			row[xx] = c;
		}
	}
}

static inline void aster_gfx_frame_rect(aster_gfx_t *g, uint32_t x, uint32_t y, uint32_t w,
				      uint32_t h, uint16_t c)
{
	for (uint32_t xx = x; xx < x + w; xx++) {
		aster_gfx_pixel(g, xx, y, c);
		aster_gfx_pixel(g, xx, y + h - 1, c);
	}
	for (uint32_t yy = y; yy < y + h; yy++) {
		aster_gfx_pixel(g, x, yy, c);
		aster_gfx_pixel(g, x + w - 1, yy, c);
	}
}

static inline void aster_gfx_hline(aster_gfx_t *g, uint32_t x0, uint32_t x1, uint32_t y, uint16_t c)
{
	if (x0 > x1) {
		uint32_t t = x0;
		x0 = x1;
		x1 = t;
	}
	for (uint32_t x = x0; x <= x1 && x < g->w; x++) {
		aster_gfx_pixel(g, x, y, c);
	}
}

static inline void aster_gfx_vline(aster_gfx_t *g, uint32_t x, uint32_t y0, uint32_t y1, uint16_t c)
{
	if (y0 > y1) {
		uint32_t t = y0;
		y0 = y1;
		y1 = t;
	}
	for (uint32_t y = y0; y <= y1 && y < g->h; y++) {
		aster_gfx_pixel(g, x, y, c);
	}
}

static inline void aster_gfx_line(aster_gfx_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
				uint16_t c)
{
	int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
	int32_t sx = x0 < x1 ? 1 : -1;
	int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
	int32_t sy = y0 < y1 ? 1 : -1;
	int32_t err = dx + dy;

	for (;;) {
		if (x0 >= 0 && y0 >= 0) {
			aster_gfx_pixel(g, (uint32_t)x0, (uint32_t)y0, c);
		}
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int32_t e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static inline void aster_gfx_disk(aster_gfx_t *g, int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
	for (int32_t y = -r; y <= r; y++) {
		for (int32_t x = -r; x <= r; x++) {
			if (x * x + y * y <= r * r) {
				aster_gfx_pixel(g, (uint32_t)(cx + x), (uint32_t)(cy + y), c);
			}
		}
	}
}

/* 5x7 bitmap font covering printable ASCII 0x20..0x7E. Each glyph is 7 rows
 * of 5 bits (bit4 = leftmost). */
extern const uint8_t aster_font5x7[95][7];

static inline void aster_gfx_char(aster_gfx_t *g, uint32_t x, uint32_t y, char ch, uint16_t fg,
				uint16_t bg, int invert_bg)
{
	if ((unsigned)ch < 0x20u || (unsigned)ch > 0x7Eu) {
		ch = '?';
	}
	const uint8_t *glyph = aster_font5x7[ch - 0x20];
	for (int row = 0; row < 7; row++) {
		uint8_t bits = glyph[row];
		for (int col = 0; col < 5; col++) {
			int on = bits & (1u << (4 - col));
			if (on) {
				aster_gfx_pixel(g, x + col, y + row, fg);
			} else if (invert_bg) {
				aster_gfx_pixel(g, x + col, y + row, bg);
			}
		}
	}
}

static inline uint32_t aster_gfx_text(aster_gfx_t *g, uint32_t x, uint32_t y, const char *s,
				    uint16_t fg, uint16_t bg, int bg_fill)
{
	uint32_t cx = x;
	for (; *s; s++) {
		if (*s == '\n') {
			cx = x;
			y += 8;
			continue;
		}
		aster_gfx_char(g, cx, y, *s, fg, bg, bg_fill);
		cx += 6;
	}
	return cx - x;
}

#endif /* ASTER_GFX_H */
