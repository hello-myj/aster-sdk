"""Execute SDK Scene/layout/widgets against a pixel-checking transactional Host."""
import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
HARNESS=r'''
#include <assert.h>
#include <string.h>
#include <aster_widgets.h>
#define W 32
#define H 32
static uint16_t screen[W*H],guest[W*H],draft[W*H];
static uint32_t epoch,caps=7,token,published,queries,draws,cancelled,metrics;
static int active,fail_next,ready_bitmap=1;
static uint32_t cx,cy,cw=W,ch=H;
static aster_scene_t scene;static aster_scene_node_t nodes[32];static aster_ge_t ge;
static aster_gfx_cmd_t ring[2];static uint8_t arena[128];
static uint16_t blend(uint16_t d,uint16_t s,uint32_t a){
 unsigned r=(((d>>11)&31)*(255-a)+((s>>11)&31)*a)/255;
 unsigned g=(((d>>5)&63)*(255-a)+((s>>5)&63)*a)/255;
 unsigned b=((d&31)*(255-a)+(s&31)*a)/255;return (uint16_t)((r<<11)|(g<<5)|b);
}
static void put(int x,int y,uint16_t c){if(x>=0&&y>=0&&x<W&&y<H&&(uint32_t)x>=cx&&(uint32_t)y>=cy&&(uint32_t)x<cx+cw&&(uint32_t)y<cy+ch)draft[y*W+x]=c;}
int32_t aster_gfx_text_w(uint32_t px,const char *t,uint32_t len){(void)px;(void)t;metrics++;return (int32_t)len;}
int32_t aster_gfx_exec(aster_handle_t surf,const aster_gfx_cmd_t *cmd,uint32_t n,const uint8_t *data,uint32_t len,uint32_t x,uint32_t y,uint32_t w,uint32_t h){
 (void)surf;(void)x;(void)y;(void)w;(void)h;
 if(n==ASTER_GFX_FRAME_CONTROL){
  if(len==32){const aster_gfx_frame_clip_t *c=(const void*)data;assert(active&&c->frame_id==token);assert(c->w&&c->h&&c->x+c->w<=W&&c->y+c->h<=H);cx=c->x;cy=c->y;cw=c->w;ch=c->h;return 0;}
  aster_gfx_frame_control_t *c=(void*)data;
  if(c->action==0){queries++;c->flags=caps;c->frame_id=epoch;return 0;}
  if(c->action==1){assert(!active);active=1;token++;memcpy(draft,guest,sizeof(guest));cx=cy=0;cw=W;ch=H;c->frame_id=token;return 0;}
  assert(active&&c->frame_id==token);active=0;
  if(c->action==2){memcpy(screen,draft,sizeof(screen));memcpy(guest,draft,sizeof(guest));published++;}else cancelled++;
  return 0;
 }
 assert(active);draws++;if(fail_next){fail_next=0;return ASTER_EIO;}
 for(uint32_t i=0;i<n;i++){
  const uint32_t *v=cmd[i].w;int op=ASTER_GFX_OP(&cmd[i]),xx=ASTER_GFX_P16(v[1]),yy=ASTER_GFX_P16H(v[1]);
  if(op==ASTER_GFX_CLEAR){for(int a=0;a<W;a++)for(int b=0;b<H;b++)put(a,b,(uint16_t)v[1]);}
  else if(op==ASTER_GFX_RECT_F){for(uint32_t a=0;a<ASTER_GFX_U16(v[2]);a++)for(uint32_t b=0;b<ASTER_GFX_HI16(v[2]);b++)put(xx+a,yy+b,(uint16_t)v[3]);}
  else if(op==ASTER_GFX_TEXT){assert(v[3]<=len&&v[4]<=len-v[3]);if(ready_bitmap)for(uint32_t a=0;a<v[4];a++)put(xx+a,yy-aster_text_asc(ASTER_GFX_U16(v[2])),ASTER_GFX_HI16(v[2]));}
  else {assert(op>=9&&op<=12);if(v[2]!=42)return ASTER_EBADF;
   uint32_t scale=op==10||op==12?v[4]&4095:256,alpha=op==11?v[4]&511:op==12?v[5]&511:255;
   uint16_t tex[4]={0xf800,0,0x07e0,0x001f};uint32_t dw=2*scale/256,dh=2*scale/256;
   for(uint32_t a=0;a<dw;a++)for(uint32_t b=0;b<dh;b++){
    uint16_t color=tex[(b*2/dh)*2+a*2/dw];int dx=xx+a,dy=yy+b;
    if(color&&dx>=0&&dy>=0&&dx<W&&dy<H)put(dx,dy,blend(draft[dy*W+dx],color,alpha));
   }
  }
 }return (int32_t)n;
}
static void setup(uint32_t capacity){
 memset(screen,0,sizeof(screen));memset(guest,0,sizeof(guest));epoch=0;caps=7;active=fail_next=0;ready_bitmap=1;published=queries=draws=cancelled=metrics=0;
 aster_ge_init(&ge,1,W,H,ring,2,arena,sizeof(arena));assert(aster_scene_init(&scene,&ge,nodes,capacity,0,0x1234)==0);
}
static inline uint32_t rect(uint32_t parent,aster_box_t b,uint16_t c){uint32_t id;assert(!aster_scene_add(&scene,parent,ASTER_NODE_RECT,&id));assert(!aster_scene_set_box(&scene,id,b));assert(!aster_scene_set_color(&scene,id,c,true));return id;}
'''
class Scene(unittest.TestCase):
 def run_c(self,body):
  with tempfile.TemporaryDirectory() as d:
   exe=Path(d)/'check'
   p=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/aster'),'-x','c','-',*[str(ROOT/'lib/aster'/n) for n in ('aster_scene.c','aster_layout.c','aster_widgets.c')],'-o',str(exe)],input=HARNESS+body,text=True,capture_output=True)
   self.assertEqual(p.returncode,0,p.stderr);subprocess.run([str(exe)],check=True,timeout=10)
 def test_damage_z_clipping_alpha_removal_pool_and_atomic_failure(self):
  self.run_c(r'''
int main(void){
 setup(8);uint32_t a=rect(0,(aster_box_t){1,1,5,5},0xf800),b=rect(0,(aster_box_t){3,3,4,4},0x07e0);
 assert(aster_scene_render(&scene)==1&&screen[3*W+3]==0x07e0);
 unsigned before=published,calls=draws;assert(!aster_scene_render(&scene)&&published==before&&draws==calls);
 assert(!aster_scene_set_box(&scene,a,(aster_box_t){8,1,5,5}));assert(aster_scene_render(&scene)==1);
 assert(screen[1*W+1]==0x1234&&screen[3*W+3]==0x07e0&&screen[1*W+8]==0xf800);
 assert(!aster_scene_set_visible(&scene,b,false));assert(aster_scene_render(&scene)==1&&screen[3*W+3]==0x1234);
 uint32_t parent,image;assert(!aster_scene_add(&scene,0,ASTER_NODE_CONTAINER,&parent));assert(!aster_scene_set_box(&scene,parent,(aster_box_t){10,10,2,2}));
 assert(!aster_scene_add(&scene,parent,ASTER_NODE_IMAGE,&image));assert(!aster_scene_set_box(&scene,image,(aster_box_t){-1,-1,4,4}));
 assert(!aster_scene_set_image(&scene,image,42,2,2,512));assert(!aster_scene_set_opacity(&scene,image,128));assert(aster_scene_render(&scene)==1);
 assert(screen[10*W+10]==blend(0x1234,0xf800,128)&&screen[9*W+9]==0x1234);
 assert(!aster_scene_remove(&scene,parent));assert(aster_scene_render(&scene)==1&&screen[10*W+10]==0x1234);
 assert(aster_scene_set_visible(&scene,image,true)==ASTER_EBADF);
 assert(!aster_scene_set_color(&scene,a,0xffff,true));uint16_t saved[W*H];memcpy(saved,screen,sizeof(saved));fail_next=1;assert(aster_scene_render(&scene)==ASTER_EIO);assert(!memcmp(screen,saved,sizeof(saved))&&cancelled);
 assert(aster_scene_render(&scene)==1&&screen[1*W+8]==0xffff);
 uint32_t id;while(aster_scene_add(&scene,0,ASTER_NODE_RECT,&id)==0){}assert(aster_scene_add(&scene,0,ASTER_NODE_RECT,&id)==ASTER_ENOSPC);
 assert(!aster_scene_remove(&scene,a));assert(!aster_scene_add(&scene,0,ASTER_NODE_RECT,&id)&&id!=a);assert(aster_scene_set_color(&scene,a,0,true)==ASTER_EBADF);
 assert(!aster_scene_remove(&scene,id));scene.next_id=UINT32_MAX;assert(aster_scene_add(&scene,0,ASTER_NODE_RECT,&id)==ASTER_ENOSPC);return 0;
}
''')
 def test_font_epoch_survives_other_draw_ack_and_cache_is_quiet(self):
  self.run_c(r'''
int main(void){
 setup(4);ready_bitmap=0;uint32_t id;assert(!aster_scene_add(&scene,0,ASTER_NODE_TEXT,&id));assert(!aster_scene_set_text(&scene,id,"abc",12,0xffff));assert(!aster_scene_set_box(&scene,id,(aster_box_t){1,1,0,0}));
 assert(aster_scene_render(&scene)==1);unsigned measured=metrics,drawn=draws;assert(screen[W+1]==0x1234);
 for(int i=0;i<3;i++)assert(!aster_scene_render(&scene));assert(metrics==measured&&draws==drawn);
 /* Another renderer acknowledged its repaint edge; only the epoch remains. */
 epoch++;ready_bitmap=1;assert(aster_scene_render(&scene)==1&&metrics==measured+1&&screen[W+1]==0xffff);
 assert(!aster_scene_render(&scene));
 unsigned cached=metrics;assert(!aster_scene_set_box(&scene,id,(aster_box_t){3,1,0,0}));
 assert(aster_scene_render(&scene)==1&&metrics==cached&&screen[W+1]==0x1234&&screen[W+3]==0xffff);
 caps=1;aster_scene_t old;aster_scene_node_t pool[1];assert(aster_scene_init(&old,&ge,pool,1,0,0)==ASTER_ENOTSUP);return 0;
}
''')
 def test_layout_atomic_overflow_and_scroll_cancels_button_click(self):
  self.run_c(r'''
int main(void){
 aster_layout_t l={ASTER_LAYOUT_ROW,ASTER_ALIGN_CENTER,0,0,1,1};aster_layout_item_t items[2]={{2,3,1},{2,3,3}};aster_box_t out[2],old[2];int32_t cw=99,ch=99;
 assert(!aster_layout_compute(&l,11,8,items,2,out,&cw,&ch));assert(out[0].w==3&&out[1].w==5&&out[1].x==5&&out[0].y==2);
 memcpy(old,out,sizeof(out));cw=ch=99;assert(aster_layout_compute(&l,3,8,items,2,out,&cw,&ch)==ASTER_ENOSPC);assert(!memcmp(out,old,sizeof(out))&&cw==99&&ch==99);
 setup(12);uint32_t parent;assert(!aster_scene_add(&scene,0,ASTER_NODE_CONTAINER,&parent));assert(!aster_scene_set_box(&scene,parent,(aster_box_t){0,0,20,20}));l=(aster_layout_t){ASTER_LAYOUT_COLUMN,ASTER_ALIGN_START,0,1,0,2};assert(!aster_scene_set_layout(&scene,parent,l));
 aster_widgets_t widgets;aster_button_t buttons[2];assert(!aster_widgets_init(&widgets,&scene,buttons,2));uint32_t first,second;assert(!aster_widgets_button(&widgets,parent,(aster_box_t){0,0,20,12},"one",12,0xffff,0x2222,0x3333,&first));assert(!aster_widgets_button(&widgets,parent,(aster_box_t){0,0,20,12},"two",12,0xffff,0x2222,0x3333,&second));assert(aster_scene_render(&scene)==1);
 aster_event_t e={.type=ASTER_EVENT_TOUCH};uint32_t clicked;e.u.touch.x=5;e.u.touch.y=5;e.u.touch.action=ASTER_TOUCH_DOWN;assert(!aster_widgets_event(&widgets,&e,&clicked));e.u.touch.action=ASTER_TOUCH_UP;assert(aster_widgets_event(&widgets,&e,&clicked)==1&&clicked==first);
 assert(aster_scene_render(&scene)==1);e.u.touch.y=17;e.u.touch.action=ASTER_TOUCH_DOWN;assert(!aster_widgets_event(&widgets,&e,&clicked));e.u.touch.y=2;e.u.touch.action=ASTER_TOUCH_MOVE;assert(!aster_widgets_event(&widgets,&e,&clicked));e.u.touch.action=ASTER_TOUCH_UP;assert(!aster_widgets_event(&widgets,&e,&clicked)&&!clicked);assert(aster_scene_render(&scene)==1);
 aster_scene_node_t *p=NULL;for(int i=0;i<12;i++)if(nodes[i].id==parent)p=&nodes[i];assert(p&&p->scroll_y==6);assert(aster_scene_hit_test(&scene,5,25)==0);
 uint16_t saved[W*H];memcpy(saved,screen,sizeof(saved));l.scroll=0;assert(!aster_scene_set_layout(&scene,parent,l));assert(aster_scene_render(&scene)==ASTER_ENOSPC&&!memcmp(saved,screen,sizeof(saved)));l.scroll=1;assert(!aster_scene_set_layout(&scene,parent,l));assert(aster_scene_render(&scene)>=0);return 0;
}
''')

 def test_grid_canvas_cache_depth_and_invalid_text(self):
  self.run_c(r'''int main(void){
 aster_layout_t grid={ASTER_LAYOUT_GRID,ASTER_ALIGN_STRETCH,2,0,1,1};
 aster_layout_item_t items[3]={{0,4,0},{0,2,0},{0,3,0}};aster_box_t out[3];int32_t cw,ch;
 assert(!aster_layout_compute(&grid,15,12,items,3,out,&cw,&ch));
 assert(out[0].x==1&&out[0].w==6&&out[0].h==4&&out[1].x==8&&out[2].y==6&&ch==10);
 setup(32);scene.flags=ASTER_SCENE_CANVAS;uint32_t text;
 assert(!aster_scene_add(&scene,0,ASTER_NODE_TEXT,&text));assert(!aster_scene_set_text(&scene,text,"abc",12,0xffff));
 assert(!aster_scene_set_box(&scene,text,(aster_box_t){1,1,6,0}));assert(aster_scene_render(&scene)==1);
 unsigned measured=metrics;assert(!scene.layout_dirty);
 assert(!aster_scene_set_text(&scene,text,"abc",12,0x07e0));assert(!scene.layout_dirty);
 assert(aster_scene_render(&scene)==1&&metrics==measured&&screen[W+1]==0x07e0);
 assert(aster_scene_set_text(&scene,text,"a\nb",12,0)==ASTER_EINVAL);
 assert(!aster_scene_set_visible(&scene,text,false));assert(aster_scene_render(&scene)==1&&screen[W+1]==0x1234);
 assert(!aster_scene_set_visible(&scene,text,true));assert(aster_scene_render(&scene)==1&&screen[W+1]==0x07e0);
 assert(aster_scene_set_opacity(&scene,text,128)==ASTER_ENOTSUP);
 uint32_t parent=0,id=0;for(unsigned i=0;i<ASTER_SCENE_MAX_DEPTH;i++){
  assert(!aster_scene_add(&scene,parent,ASTER_NODE_CONTAINER,&id));parent=id;
 }
 assert(aster_scene_add(&scene,parent,ASTER_NODE_CONTAINER,&id)==ASTER_ENOSPC);
 return 0;
}
''')
