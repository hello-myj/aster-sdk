/* SPDX-License-Identifier: MIT */
#include <aster/aster.h>
#include "aster_widgets.h"
#include "aster_printf.h"
#define TRY(e) do{int32_t result=(e);if(result<0)return result;}while(0)
static aster_handle_t surface;
static aster_ge_t ge;static aster_gfx_cmd_t ring[16];static uint8_t arena[384];
static aster_scene_t scene;static aster_scene_node_t nodes[32];
static aster_widgets_t widgets;static aster_button_t buttons[10];
static aster_node_id_t rows[10],status;static uint8_t values[10],body;
static uint32_t frames,publishes,clicks,errors;
static const char *names[]={"动画效果","深色外观","状态提示","自动保存","保持屏幕","通知提醒","触摸反馈","背景纹理","音量提示","图标标签"};
static int label(uint32_t parent,aster_box_t b,const char *text,uint8_t px,uint16_t color,uint32_t *id){
 TRY(aster_scene_add(&scene,parent,ASTER_NODE_TEXT,id));TRY(aster_scene_set_box(&scene,*id,b));return aster_scene_set_text(&scene,*id,text,px,color);
}
int aster_app_init(void){
 aster_surface_desc_t d={.size=sizeof(d),.version=1,.format=ASTER_PIXEL_RGB565};TRY(aster_surface_create(&d,sizeof(d),&surface));
 aster_surface_buffer_t b={.size=sizeof(b),.version=1};TRY(aster_surface_acquire(surface,&b,sizeof(b)));
 aster_ge_init(&ge,surface,b.width,b.height,ring,16,arena,sizeof(arena));
 TRY(aster_scene_init(&scene,&ge,nodes,32,b.flags,0x0842));TRY(aster_widgets_init(&widgets,&scene,buttons,10));
 body=aster_text_for_display(16,b.width,b.height);uint8_t heading=aster_text_for_display(26,b.width,b.height);uint32_t title,sub,list;
 TRY(label(0,(aster_box_t){12,12,b.width-24,32},"设置",heading,0xef7d,&title));
 TRY(label(0,(aster_box_t){12,44,b.width-24,24},"轻滑列表 · 点按切换",body,0x8cb2,&sub));
 TRY(aster_scene_add(&scene,0,ASTER_NODE_CONTAINER,&list));TRY(aster_scene_set_box(&scene,list,(aster_box_t){8,76,b.width-16,b.height-128}));
 TRY(aster_scene_set_layout(&scene,list,(aster_layout_t){ASTER_LAYOUT_COLUMN,ASTER_ALIGN_STRETCH,0,1,0,6}));
 for(unsigned i=0;i<10;i++){
  char text[80];aster_snprintf(text,sizeof(text),"%s     关",names[i]);
  TRY(aster_widgets_button(&widgets,list,(aster_box_t){0,0,b.width-16,42},text,body,0xe75c,0x18e5,0x2a29,&rows[i]));
 }
 TRY(label(0,(aster_box_t){12,b.height-40,b.width-24,26},"尚未修改",body,0x7e95,&status));aster_app_set_framerate(30);
 aster_logf(ASTER_LOG_INFO,"G7SET INIT %ux%u nodes=24",b.width,b.height);return 0;
}
void aster_app_event(const aster_event_t *e){
 if(e->type==ASTER_EVENT_TOUCH){uint32_t clicked=0;int r=aster_widgets_event(&widgets,e,&clicked);if(r<0)errors++;
  if(clicked)for(unsigned i=0;i<10;i++)if(rows[i]==clicked){
   values[i]^=1;clicks++;char text[80];aster_snprintf(text,sizeof(text),"%s     %s",names[i],values[i]?"开":"关");
   (void)aster_scene_set_text(&scene,buttons[i].label,text,body,0xe75c);
   aster_snprintf(text,sizeof(text),"已修改 %u 次",clicks);(void)aster_scene_set_text(&scene,status,text,body,0x7e95);
   aster_logf(ASTER_LOG_INFO,"G7SET CLICK index=%u value=%u",i,values[i]);break;
  }
  return;
 }
 if(e->type!=ASTER_EVENT_FRAME)return;frames++;int r=aster_scene_render(&scene);if(r>0)publishes++;else if(r<0&&r!=ASTER_EBUSY){errors++;if(errors<=3)aster_logf(ASTER_LOG_ERROR,"G7SET render=%d",r);}
 if(frames%150==0)aster_logf(ASTER_LOG_INFO,"G7SET frames=%u publishes=%u clicks=%u errors=%u",frames,publishes,clicks,errors);
}
void aster_app_pause(void){aster_widgets_cancel(&widgets);}
void aster_app_resume(void){aster_widgets_cancel(&widgets);aster_scene_invalidate(&scene);}
void aster_app_stop(void){aster_logf(ASTER_LOG_INFO,"G7SET END frames=%u publishes=%u clicks=%u errors=%u",frames,publishes,clicks,errors);}
void aster_app_destroy(void){if(surface)aster_surface_destroy(surface);surface=0;}
