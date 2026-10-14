/* SPDX-License-Identifier: MIT */
#ifndef ASTER_SCENE_H
#define ASTER_SCENE_H
#include "aster_libc.h"
#include "aster_gfx_engine.h"
#include "aster_layout.h"
#define ASTER_SCENE_TEXT_BYTES 128u
#define ASTER_SCENE_MAX_DEPTH 16u
#define ASTER_SCENE_CANVAS 1u

typedef uint32_t aster_node_id_t;
enum { ASTER_NODE_CONTAINER=1, ASTER_NODE_RECT, ASTER_NODE_IMAGE, ASTER_NODE_TEXT };
/* Storage is caller-owned; mutate only through the functions below. IDs are
 * scoped to their Scene, never reused, and zero denotes its implicit root. */
typedef struct {
 uint32_t id,parent,kind,image,image_w,image_h;
 int32_t z,scroll_x,scroll_y,content_w,content_h,text_width;
 aster_box_t requested,last_world,last_clip;
 aster_layout_t layout;
 uint16_t color,scale,grow;
 uint8_t px,opacity,text_align;
 bool used,visible,changed,measured,interactive,background;
 char text[ASTER_SCENE_TEXT_BYTES];
} aster_scene_node_t;
typedef struct {
 aster_ge_t *ge;aster_scene_node_t *nodes;
 uint32_t capacity,next_id,epoch,flags,order_count,work_count;
 uint16_t background;
 bool ready,force_full,dirty,layout_dirty;
 aster_box_t damage;
 uint16_t order[ASTER_SCENE_LIMIT],work_order[ASTER_SCENE_LIMIT],indices[ASTER_SCENE_LIMIT];
 aster_box_t local[ASTER_SCENE_LIMIT],world[ASTER_SCENE_LIMIT],clip[ASTER_SCENE_LIMIT],layout_out[ASTER_SCENE_LIMIT];
 aster_layout_item_t layout_in[ASTER_SCENE_LIMIT];
 int32_t work_cw[ASTER_SCENE_LIMIT],work_ch[ASTER_SCENE_LIMIT];
} aster_scene_t;
int32_t aster_scene_init(aster_scene_t *,aster_ge_t *,aster_scene_node_t *,uint32_t capacity,uint32_t surface_flags,uint16_t background);
int32_t aster_scene_add(aster_scene_t *,aster_node_id_t parent,uint32_t kind,aster_node_id_t *out);
int32_t aster_scene_remove(aster_scene_t *,aster_node_id_t);
int32_t aster_scene_set_box(aster_scene_t *,aster_node_id_t,aster_box_t);
int32_t aster_scene_set_text(aster_scene_t *,aster_node_id_t,const char *,uint8_t px,uint16_t color);
int32_t aster_scene_set_color(aster_scene_t *,aster_node_id_t,uint16_t color,bool background);
int32_t aster_scene_set_image(aster_scene_t *,aster_node_id_t,uint32_t handle,uint32_t w,uint32_t h,uint16_t scale256);
int32_t aster_scene_set_visible(aster_scene_t *,aster_node_id_t,bool);
/* Intermediate opacity is supported for IMAGE only. Other kinds accept 0/255. */
int32_t aster_scene_set_opacity(aster_scene_t *,aster_node_id_t,uint8_t);
int32_t aster_scene_set_z(aster_scene_t *,aster_node_id_t,int32_t);
int32_t aster_scene_set_layout(aster_scene_t *,aster_node_id_t,aster_layout_t);
int32_t aster_scene_set_grow(aster_scene_t *,aster_node_id_t,uint16_t);
int32_t aster_scene_set_scroll(aster_scene_t *,aster_node_id_t,int32_t x,int32_t y);
int32_t aster_scene_set_interactive(aster_scene_t *,aster_node_id_t,bool);
int32_t aster_scene_set_text_align(aster_scene_t *,aster_node_id_t,uint8_t);
/* Poll before other drawing. Epoch is independent of global font repaint ACK;
 * render also polls, so unrelated submissions cannot lose a font-ready edge. */
int32_t aster_scene_poll(aster_scene_t *);
void aster_scene_invalidate(aster_scene_t *);
/* 0 clean/no DRAW or END, 1 published, negative error (dirty state retained).
 * Sole renderer of its surface. On ENOTSUP upgrade Host; no silent fallback.
 * A canvas repaints in full after every mutation; ordinary surfaces replay
 * intersections with a conservative old/new damage union in one transaction. */
int32_t aster_scene_render(aster_scene_t *);
aster_node_id_t aster_scene_hit_test(const aster_scene_t *,int32_t x,int32_t y);
/* Committed, clipped geometry, suitable for input; not speculative layout. */
int32_t aster_scene_bounds(const aster_scene_t *,aster_node_id_t,aster_box_t *);
#endif
