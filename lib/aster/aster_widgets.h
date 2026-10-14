/* SPDX-License-Identifier: MIT */
#ifndef ASTER_WIDGETS_H
#define ASTER_WIDGETS_H
#include "aster_scene.h"
#include <aster/event.h>
#define ASTER_WIDGET_LIMIT 32u
typedef struct {aster_node_id_t id,label;uint16_t normal,pressed;} aster_button_t;
typedef struct {
 aster_scene_t *scene;aster_button_t *buttons;uint32_t capacity,count;
 aster_node_id_t capture,scroll;int32_t down_x,down_y,start_x,start_y;
 uint32_t slot;uint16_t threshold;bool active,dragged;
} aster_widgets_t;
int32_t aster_widgets_init(aster_widgets_t *,aster_scene_t *,aster_button_t *,uint32_t capacity);
int32_t aster_widgets_button(aster_widgets_t *,aster_node_id_t parent,aster_box_t,
 const char *label,uint8_t px,uint16_t fg,uint16_t normal,uint16_t pressed,aster_node_id_t *out);
/* Existing APP-space samples only; no new touch driver or system gestures.
 * Returns 1 and a clicked button ID only on a matching release without drag;
 * 0 no click, negative error. Any movement beyond threshold cancels a click.
 * Scroll containers are Scene containers with layout.scroll=1. */
int32_t aster_widgets_event(aster_widgets_t *,const aster_event_t *,aster_node_id_t *clicked);
/* Call on pause/resume or after a system gesture consumes pointer release. */
void aster_widgets_cancel(aster_widgets_t *);
#endif
