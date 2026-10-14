/* SPDX-License-Identifier: MIT */
#include <aster/aster.h>
#include "aster_scene.h"
#include "aster_printf.h"
#define TRY(e) do{int32_t result=(e);if(result<0)return result;}while(0)
static aster_handle_t surface;static aster_ge_t ge;static aster_gfx_cmd_t ring[16];static uint8_t arena[256];
static aster_scene_t scene;static aster_scene_node_t nodes[16];
static uint32_t texture,sw,sh,view,sprites[6],status,W,H,tick,frames,publishes,errors,last_rebuild;static uint8_t body;static bool paused;
static void le16(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;}
static void le32(uint8_t *p,uint32_t x){le16(p,x);le16(p+2,x>>16);}
/* Generate this demo's own tiny keyed texture once; no external art needed. */
static int texture_create(void){
 uint8_t bmp[54+16*16*3];memset(bmp,0,sizeof(bmp));bmp[0]='B';bmp[1]='M';le32(bmp+2,sizeof(bmp));le32(bmp+10,54);le32(bmp+14,40);le32(bmp+18,16);le32(bmp+22,16);le16(bmp+26,1);le16(bmp+28,24);le32(bmp+34,16*16*3);
 for(int y=0;y<16;y++)for(int x=0;x<16;x++)if((x>=6&&x<10)||(y>=6&&y<10)||((x-8)*(x-8)+(y-8)*(y-8)<36)){
  uint8_t *p=bmp+54+((15-y)*16+x)*3;p[0]=100;p[1]=200+y*3;p[2]=80+x*8;
 }
 const char *path="/data/star.bmp";aster_handle_t file;TRY(aster_fs_open(path,14,ASTER_FS_O_WRONLY|ASTER_FS_O_CREAT|ASTER_FS_O_TRUNC,&file));
 uint32_t off=0;while(off<sizeof(bmp)){int r=aster_fs_write(file,bmp+off,sizeof(bmp)-off);if(r<=0){aster_fs_close(file);return r?r:ASTER_EIO;}off+=r;}
 TRY(aster_fs_close(file));return aster_gfx_bmp_load(path,14,32,32,&texture,&sw,&sh);
}
static int add_sprite(unsigned i){
 TRY(aster_scene_add(&scene,view,ASTER_NODE_IMAGE,&sprites[i]));TRY(aster_scene_set_image(&scene,sprites[i],texture,sw,sh,(uint16_t)(256+i*64)));
 return aster_scene_set_opacity(&scene,sprites[i],(uint8_t)(255-i*20));
}
int aster_app_init(void){
 aster_surface_desc_t d={.size=sizeof(d),.version=1,.format=ASTER_PIXEL_RGB565};TRY(aster_surface_create(&d,sizeof(d),&surface));
 aster_surface_buffer_t b={.size=sizeof(b),.version=1};TRY(aster_surface_acquire(surface,&b,sizeof(b)));W=b.width;H=b.height;body=aster_text_for_display(16,W,H);
 aster_ge_init(&ge,surface,W,H,ring,16,arena,sizeof(arena));TRY(aster_scene_init(&scene,&ge,nodes,16,b.flags,0x0842));TRY(texture_create());
 uint32_t title;TRY(aster_scene_add(&scene,0,ASTER_NODE_TEXT,&title));TRY(aster_scene_set_box(&scene,title,(aster_box_t){12,12,W-24,36}));TRY(aster_scene_set_text(&scene,title,"星点穿梭",aster_text_for_display(24,W,H),0xef7d));
 TRY(aster_scene_add(&scene,0,ASTER_NODE_CONTAINER,&view));TRY(aster_scene_set_box(&scene,view,(aster_box_t){8,58,W-16,H-110}));TRY(aster_scene_set_color(&scene,view,0x10c4,true));
 for(unsigned i=0;i<6;i++)TRY(add_sprite(i));TRY(aster_scene_add(&scene,0,ASTER_NODE_TEXT,&status));TRY(aster_scene_set_box(&scene,status,(aster_box_t){12,H-40,W-24,26}));TRY(aster_scene_set_text(&scene,status,"轻点暂停 · 再点继续",body,0x7e95));
 aster_app_set_framerate(30);aster_logf(ASTER_LOG_INFO,"G7SPR INIT %ux%u texture=%u",W,H,texture);return 0;
}
void aster_app_event(const aster_event_t *e){
 if(e->type==ASTER_EVENT_TOUCH&&e->u.touch.action==ASTER_TOUCH_UP){paused=!paused;(void)aster_scene_set_text(&scene,status,paused?"已暂停 · 轻点继续":"轻点暂停 · 再点继续",body,0x7e95);return;}
 if(e->type!=ASTER_EVENT_FRAME)return;frames++;
 if(!paused){
  if(tick&&tick%240==0&&last_rebuild!=tick){last_rebuild=tick;(void)aster_scene_remove(&scene,sprites[5]);int r=add_sprite(5);if(r)errors++;}
  for(unsigned i=0;i<6;i++){
   int32_t x=(int32_t)((tick*(i+1)+i*31)%(W+40))-36,y=(int32_t)((tick*(i+2)+i*23)%(H-92))-18;
   (void)aster_scene_set_box(&scene,sprites[i],(aster_box_t){x,y,0,0});(void)aster_scene_set_visible(&scene,sprites[i],i!=4||tick%120<90);
  }
 }
 int r=aster_scene_render(&scene);if(r>0){publishes++;if(!paused)tick++;}else if(r<0&&r!=ASTER_EBUSY){errors++;if(errors<=3)aster_logf(ASTER_LOG_ERROR,"G7SPR render=%d",r);}
 if(frames%150==0)aster_logf(ASTER_LOG_INFO,"G7SPR frames=%u publishes=%u tick=%u paused=%u errors=%u",frames,publishes,tick,paused,errors);
}
void aster_app_resume(void){aster_scene_invalidate(&scene);}
void aster_app_stop(void){aster_logf(ASTER_LOG_INFO,"G7SPR END frames=%u tick=%u errors=%u",frames,tick,errors);}
void aster_app_destroy(void){if(texture)aster_gfx_bmp_free(texture);if(surface)aster_surface_destroy(surface);texture=surface=0;}
