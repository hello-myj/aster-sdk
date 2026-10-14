/* SPDX-License-Identifier: MIT */
#include "aster_widgets.h"
static aster_scene_node_t *node(aster_scene_t *s,uint32_t id){for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used&&s->nodes[i].id==id)return &s->nodes[i];return NULL;}
static void pressed(aster_widgets_t *w,uint32_t id,bool on){for(uint32_t i=0;i<w->count;i++)if(w->buttons[i].id==id){(void)aster_scene_set_color(w->scene,id,on?w->buttons[i].pressed:w->buttons[i].normal,true);break;}}
int32_t aster_widgets_init(aster_widgets_t *w,aster_scene_t *s,aster_button_t *b,uint32_t cap){
 if(!w||!s||!s->ready||!b||!cap||cap>ASTER_WIDGET_LIMIT)return ASTER_EINVAL;memset(w,0,sizeof(*w));w->scene=s;w->buttons=b;w->capacity=cap;w->threshold=8;memset(b,0,sizeof(*b)*cap);return 0;
}
int32_t aster_widgets_button(aster_widgets_t *w,uint32_t parent,aster_box_t box,const char *text,uint8_t px,uint16_t fg,uint16_t normal,uint16_t down,uint32_t *out){
 if(!w||!w->scene||!out)return ASTER_EINVAL;
 /* Reclaim bindings for buttons removed from the Scene. */
 for(uint32_t i=0;i<w->count;)if(!node(w->scene,w->buttons[i].id))w->buttons[i]=w->buttons[--w->count];else i++;
 if(w->count==w->capacity)return ASTER_ENOSPC;
 uint32_t id,label;int r=aster_scene_add(w->scene,parent,ASTER_NODE_CONTAINER,&id);if(r)return r;
 if((r=aster_scene_set_box(w->scene,id,box))||(r=aster_scene_set_color(w->scene,id,normal,true))||
    (r=aster_scene_set_interactive(w->scene,id,true))||(r=aster_scene_add(w->scene,id,ASTER_NODE_TEXT,&label))||
    (r=aster_scene_set_text(w->scene,label,text,px,fg))||
    (r=aster_scene_set_box(w->scene,label,(aster_box_t){4,0,box.w>8?box.w-8:0,box.h}))||
    (r=aster_scene_set_text_align(w->scene,label,1))){(void)aster_scene_remove(w->scene,id);return r;}
 w->buttons[w->count++]=(aster_button_t){id,label,normal,down};*out=id;return 0;
}
void aster_widgets_cancel(aster_widgets_t *w){if(!w)return;if(w->capture)pressed(w,w->capture,false);w->capture=w->scroll=0;w->active=w->dragged=false;}
static uint32_t scroll_at(aster_widgets_t *w,int32_t x,int32_t y){
 aster_scene_t *s=w->scene;
 for(uint32_t j=s->order_count;j;j--){aster_scene_node_t *n=&s->nodes[s->order[j-1]];aster_box_t b=n->last_clip;
  if(n->used&&n->kind==ASTER_NODE_CONTAINER&&n->layout.scroll&&b.w>0&&x>=b.x&&y>=b.y&&x<b.x+b.w&&y<b.y+b.h)return n->id;
 }return 0;
}
int32_t aster_widgets_event(aster_widgets_t *w,const aster_event_t *e,uint32_t *clicked){
 if(!w||!w->scene||!e||!clicked)return ASTER_EINVAL;*clicked=0;if(e->type!=ASTER_EVENT_TOUCH)return 0;
 if(e->u.touch.x>32767||e->u.touch.y>32767)return ASTER_EINVAL;
 int32_t x=e->u.touch.x,y=e->u.touch.y;uint32_t action=e->u.touch.action;
 if(action==ASTER_TOUCH_DOWN){
  if(w->active&&e->u.touch.slot!=w->slot)return 0;
  aster_widgets_cancel(w);w->active=true;w->slot=e->u.touch.slot;w->down_x=x;w->down_y=y;
  uint32_t hit=aster_scene_hit_test(w->scene,x,y);
  for(uint32_t i=0;i<w->count;i++)if(w->buttons[i].id==hit)w->capture=hit;
  w->scroll=scroll_at(w,x,y);aster_scene_node_t *n=node(w->scene,w->scroll);w->start_x=n?n->scroll_x:0;w->start_y=n?n->scroll_y:0;
  if(w->capture)pressed(w,w->capture,true);return 0;
 }
 if(!w->active||e->u.touch.slot!=w->slot)return 0;
 if(action!=ASTER_TOUCH_MOVE&&action!=ASTER_TOUCH_UP)return ASTER_EINVAL;
 int32_t dx=x-w->down_x,dy=y-w->down_y;
 if((int64_t)dx*dx+(int64_t)dy*dy>(int64_t)w->threshold*w->threshold){w->dragged=true;if(w->capture){pressed(w,w->capture,false);w->capture=0;}}
 if(w->dragged&&w->scroll){
  aster_scene_node_t *n=node(w->scene,w->scroll);
  if(n){int32_t sx=w->start_x-dx,sy=w->start_y-dy,maxx=n->content_w-n->last_world.w,maxy=n->content_h-n->last_world.h;
   if(maxx<0)maxx=0;if(maxy<0)maxy=0;if(sx<0)sx=0;if(sy<0)sy=0;if(sx>maxx)sx=maxx;if(sy>maxy)sy=maxy;
   int r=aster_scene_set_scroll(w->scene,n->id,sx,sy);if(r)return r;
  }
 }
 if(action==ASTER_TOUCH_UP){uint32_t capture=w->capture;if(!w->dragged&&capture&&aster_scene_hit_test(w->scene,x,y)==capture)*clicked=capture;aster_widgets_cancel(w);return *clicked?1:0;}
 return 0;
}
