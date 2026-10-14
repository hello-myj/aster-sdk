/* SPDX-License-Identifier: MIT */
#ifndef ASTER_LAYOUT_H
#define ASTER_LAYOUT_H
#include <stdint.h>
#include <stdbool.h>
#include <aster/error.h>
#define ASTER_SCENE_LIMIT 64u

typedef struct { int32_t x,y,w,h; } aster_box_t;
enum { ASTER_LAYOUT_MANUAL, ASTER_LAYOUT_ROW, ASTER_LAYOUT_COLUMN, ASTER_LAYOUT_GRID };
enum { ASTER_ALIGN_START, ASTER_ALIGN_CENTER, ASTER_ALIGN_END, ASTER_ALIGN_STRETCH };
typedef struct {
 uint8_t mode,align,columns,scroll;
 uint16_t padding,gap;
} aster_layout_t;
typedef struct { int32_t w,h; uint16_t grow; } aster_layout_item_t;
/* Atomic: no output is changed on error. Manual layout is handled by Scene.
 * ROW/COLUMN distribute nonnegative remaining pixels by grow weight. GRID
 * uses equal columns and each row's maximum preferred height. scroll permits
 * main-axis (vertical for GRID) overflow; cross-axis overflow is an error. */
int32_t aster_layout_compute(const aster_layout_t *layout,int32_t w,int32_t h,
 const aster_layout_item_t *items,uint32_t count,aster_box_t *boxes,
 int32_t *content_w,int32_t *content_h);
#endif
