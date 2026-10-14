/* SPDX-License-Identifier: MIT */
#include "aster_scene.h"
static aster_scene_node_t *find(aster_scene_t *s,uint32_t id){
 if(!s||!s->ready||!id)return NULL;
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used&&s->nodes[i].id==id)return &s->nodes[i];return NULL;
}
static aster_box_t intersection(aster_box_t a,aster_box_t b){
 int64_t x=a.x>b.x?a.x:b.x,y=a.y>b.y?a.y:b.y;
 int64_t xx=(int64_t)a.x+a.w<(int64_t)b.x+b.w?(int64_t)a.x+a.w:(int64_t)b.x+b.w;
 int64_t yy=(int64_t)a.y+a.h<(int64_t)b.y+b.h?(int64_t)a.y+a.h:(int64_t)b.y+b.h;
 return xx>x&&yy>y?(aster_box_t){x,y,xx-x,yy-y}:(aster_box_t){0};
}
static bool inside(aster_box_t b,int32_t x,int32_t y){return b.w>0&&b.h>0&&x>=b.x&&y>=b.y&&(int64_t)x<b.x+b.w&&(int64_t)y<b.y+b.h;}
static void damage(aster_scene_t *s,aster_box_t b){
 b=intersection(b,(aster_box_t){0,0,(int32_t)s->ge->w,(int32_t)s->ge->h});if(!b.w)return;
 if(!s->dirty||s->damage.w<=0)s->damage=b;
 else{int32_t x=s->damage.x<b.x?s->damage.x:b.x,y=s->damage.y<b.y?s->damage.y:b.y;
 int32_t xx=s->damage.x+s->damage.w>b.x+b.w?s->damage.x+s->damage.w:b.x+b.w;
 int32_t yy=s->damage.y+s->damage.h>b.y+b.h?s->damage.y+s->damage.h:b.y+b.h;
 s->damage=(aster_box_t){x,y,xx-x,yy-y};}s->dirty=true;
}
static int probe(aster_scene_t *s,uint32_t *epoch){
 aster_gfx_frame_control_t c={sizeof(c),1,ASTER_GFX_FRAME_QUERY,0,0,0};
 int r=aster_gfx_exec(s->ge->surf,NULL,ASTER_GFX_FRAME_CONTROL,(const uint8_t *)&c,sizeof(c),ASTER_GFX_FRAME_MAGIC,1,0,0);
 if(r==ASTER_EINVAL)return ASTER_ENOTSUP;if(r<0)return r;
 uint32_t need=ASTER_GFX_FRAME_SUPPORTED|ASTER_GFX_FRAME_CLIP_SUPPORTED|ASTER_GFX_FRAME_FONT_EPOCH_SUPPORTED;
 if((c.flags&need)!=need)return ASTER_ENOTSUP;*epoch=c.frame_id;return 0;
}
int32_t aster_scene_init(aster_scene_t *s,aster_ge_t *g,aster_scene_node_t *nodes,uint32_t cap,uint32_t flags,uint16_t bg){
 if(!s||!g||!nodes||!cap||cap>ASTER_SCENE_LIMIT||flags&~ASTER_SCENE_CANVAS||!g->w||!g->h||g->w>32767||g->h>32767||
    (uint64_t)g->w*g->h>ASTER_GFX_FRAME_MAX_PIXELS||g->frame_id||g->n||g->dirty||g->err)return ASTER_EINVAL;
 memset(s,0,sizeof(*s));s->ge=g;s->nodes=nodes;s->capacity=cap;s->flags=flags;s->background=bg;
 int r=probe(s,&s->epoch);if(r)return r;memset(nodes,0,sizeof(*nodes)*cap);s->ready=true;s->force_full=s->layout_dirty=true;return 0;
}
int32_t aster_scene_add(aster_scene_t *s,uint32_t parent,uint32_t kind,uint32_t *out){
 if(!s||!s->ready||!out||kind<ASTER_NODE_CONTAINER||kind>ASTER_NODE_TEXT)return ASTER_EINVAL;
 aster_scene_node_t *p=NULL;uint32_t depth=0,cursor=parent;
 while(cursor){p=find(s,cursor);if(!p||p->kind!=ASTER_NODE_CONTAINER)return ASTER_EBADF;cursor=p->parent;if(++depth>=ASTER_SCENE_MAX_DEPTH)return ASTER_ENOSPC;}
 if(s->next_id==UINT32_MAX)return ASTER_ENOSPC;
 for(uint32_t i=0;i<s->capacity;i++)if(!s->nodes[i].used){
  aster_scene_node_t *n=&s->nodes[i];memset(n,0,sizeof(*n));n->used=n->visible=n->changed=true;n->opacity=255;n->scale=256;n->px=16;
  n->kind=kind;n->id=++s->next_id;n->parent=parent;n->background=kind==ASTER_NODE_RECT;*out=n->id;s->layout_dirty=true;return 0;
 }
 return ASTER_ENOSPC;
}
int32_t aster_scene_remove(aster_scene_t *s,uint32_t id){
 aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;
 /* Repeated bounded passes avoid recursion and stale child-parent links. */
 uint32_t ids[ASTER_SCENE_LIMIT],count=1;ids[0]=id;
 for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<s->capacity;j++)if(s->nodes[j].used&&s->nodes[j].parent==ids[i])ids[count++]=s->nodes[j].id;
 for(uint32_t i=0;i<count;i++){n=find(s,ids[i]);if(n)n->used=false;}
 s->force_full=s->layout_dirty=true;return 0;
}
int32_t aster_scene_set_box(aster_scene_t *s,uint32_t id,aster_box_t b){
 aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;
 if(b.x<-32768||b.y<-32768||b.x>32767||b.y>32767||b.w<0||b.h<0||b.w>32767||b.h>32767)return ASTER_EINVAL;
 if(memcmp(&n->requested,&b,sizeof(b))){
  if(n->requested.w!=b.w||n->requested.h!=b.h)n->measured=false;
  n->requested=b;n->changed=true;s->layout_dirty=true;
 }return 0;
}
int32_t aster_scene_set_text(aster_scene_t *s,uint32_t id,const char *text,uint8_t px,uint16_t color){
 aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->kind!=ASTER_NODE_TEXT||!text)return ASTER_EINVAL;
 uint32_t len=0;while(len<ASTER_SCENE_TEXT_BYTES&&text[len]){if(text[len]=='\n'||text[len]=='\r')return ASTER_EINVAL;len++;}if(len==ASTER_SCENE_TEXT_BYTES)return ASTER_ENOSPC;
 px=(uint8_t)aster_text_snap(px);
 if(n->px!=px||strlen(n->text)!=len||memcmp(n->text,text,len)){
  memcpy(n->text,text,len+1);n->px=px;n->changed=true;n->measured=false;s->layout_dirty=true;
 }
 if(n->color!=color){n->color=color;n->changed=true;}
 return 0;
}
int32_t aster_scene_set_color(aster_scene_t *s,uint32_t id,uint16_t color,bool bg){
 aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;
 if(n->color!=color||n->background!=bg){n->color=color;n->background=bg;n->changed=true;}return 0;
}
int32_t aster_scene_set_image(aster_scene_t *s,uint32_t id,uint32_t handle,uint32_t w,uint32_t h,uint16_t scale){
 aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;
 if(n->kind!=ASTER_NODE_IMAGE||!handle||!w||!h||w>32767||h>32767||scale<8||scale>4095||
    (uint64_t)w*scale/256>32767||(uint64_t)h*scale/256>32767)return ASTER_EINVAL;
 if(n->image!=handle||n->image_w!=w||n->image_h!=h||n->scale!=scale){n->image=handle;n->image_w=w;n->image_h=h;n->scale=scale;n->changed=true;s->layout_dirty=true;}return 0;
}
int32_t aster_scene_set_visible(aster_scene_t *s,uint32_t id,bool value){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->visible!=value){n->visible=value;n->changed=true;s->layout_dirty=true;}return 0;}
int32_t aster_scene_set_opacity(aster_scene_t *s,uint32_t id,uint8_t value){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->kind!=ASTER_NODE_IMAGE&&value&&value!=255)return ASTER_ENOTSUP;if(n->opacity!=value){if(!n->opacity||!value)s->layout_dirty=true;n->opacity=value;n->changed=true;}return 0;}
int32_t aster_scene_set_z(aster_scene_t *s,uint32_t id,int32_t value){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->z!=value){n->z=value;n->changed=true;s->force_full=s->layout_dirty=true;}return 0;}
int32_t aster_scene_set_layout(aster_scene_t *s,uint32_t id,aster_layout_t l){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->kind!=ASTER_NODE_CONTAINER||l.mode>ASTER_LAYOUT_GRID||l.align>ASTER_ALIGN_STRETCH||l.scroll>1||(l.mode==ASTER_LAYOUT_GRID&&(!l.columns||l.columns>ASTER_SCENE_LIMIT)))return ASTER_EINVAL;if(memcmp(&n->layout,&l,sizeof(l))){n->layout=l;n->changed=true;s->layout_dirty=true;}return 0;}
int32_t aster_scene_set_grow(aster_scene_t *s,uint32_t id,uint16_t grow){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->grow!=grow){n->grow=grow;n->changed=true;s->layout_dirty=true;}return 0;}
int32_t aster_scene_set_scroll(aster_scene_t *s,uint32_t id,int32_t x,int32_t y){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->kind!=ASTER_NODE_CONTAINER||x<0||y<0||x>32767||y>32767)return ASTER_EINVAL;if(n->scroll_x!=x||n->scroll_y!=y){n->scroll_x=x;n->scroll_y=y;n->changed=true;s->layout_dirty=true;}return 0;}
int32_t aster_scene_set_interactive(aster_scene_t *s,uint32_t id,bool v){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;n->interactive=v;return 0;}
int32_t aster_scene_set_text_align(aster_scene_t *s,uint32_t id,uint8_t v){aster_scene_node_t *n=find(s,id);if(!n)return ASTER_EBADF;if(n->kind!=ASTER_NODE_TEXT||v>2)return ASTER_EINVAL;if(n->text_align!=v){n->text_align=v;n->changed=true;}return 0;}
int32_t aster_scene_poll(aster_scene_t *s){
 if(!s||!s->ready)return ASTER_ESTATE;uint32_t epoch;int r=probe(s,&epoch);if(r)return r;
 if(epoch==s->epoch)return 0;s->epoch=epoch;s->layout_dirty=true;
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used&&s->nodes[i].kind==ASTER_NODE_TEXT){s->nodes[i].changed=true;s->nodes[i].measured=false;}return 1;
}
void aster_scene_invalidate(aster_scene_t *s){if(s&&s->ready)s->force_full=true;}
static int preferred(aster_scene_node_t *n,int32_t *w,int32_t *h){
 *w=n->requested.w;*h=n->requested.h;
 if(n->kind==ASTER_NODE_TEXT){
  if(!n->measured){int r=aster_gfx_text_w(n->px,n->text,(uint32_t)strlen(n->text));if(r<0)return r;n->text_width=r;n->measured=true;}
  if(!*w)*w=n->text_width;if(!*h)*h=aster_text_line_h(n->px);
 }else if(n->kind==ASTER_NODE_IMAGE){if(!*w)*w=(uint64_t)n->image_w*n->scale/256;if(!*h)*h=(uint64_t)n->image_h*n->scale/256;}
 if(*w>32767||*h>32767)return ASTER_ENOSPC;return 0;
}
static int place_children(aster_scene_t *s,uint32_t parent,int32_t w,int32_t h){
 uint32_t count=0;
 for(uint32_t i=0;i<s->capacity;i++){aster_scene_node_t *n=&s->nodes[i];if(!n->used||n->parent!=parent||!n->visible||!n->opacity)continue;
  uint32_t j=count;while(j&&s->nodes[s->indices[j-1]].id>n->id){s->indices[j]=s->indices[j-1];j--;}s->indices[j]=i;count++;
 }
 for(uint32_t i=0;i<count;i++){
  aster_scene_node_t *n=&s->nodes[s->indices[i]];int32_t pw,ph;int r=preferred(n,&pw,&ph);if(r)return r;
  s->layout_in[i]=(aster_layout_item_t){pw,ph,n->grow};s->layout_out[i]=(aster_box_t){n->requested.x,n->requested.y,pw,ph};
 }
 aster_scene_node_t *p=parent?find(s,parent):NULL;int32_t cw=w,ch=h;
 if(p&&p->layout.mode){int r=aster_layout_compute(&p->layout,w,h,s->layout_in,count,s->layout_out,&cw,&ch);if(r)return r;}
 else for(uint32_t i=0;i<count;i++){aster_box_t b=s->layout_out[i];if(b.x+b.w>cw)cw=b.x+b.w;if(b.y+b.h>ch)ch=b.y+b.h;}
 if(p){uint32_t i=(uint32_t)(p-s->nodes);s->work_cw[i]=cw;s->work_ch[i]=ch;}
 for(uint32_t i=0;i<count;i++)s->local[s->indices[i]]=s->layout_out[i];return 0;
}
static int walk(aster_scene_t *s,uint32_t parent,aster_box_t world,aster_box_t clip,uint32_t depth){
 if(depth>ASTER_SCENE_MAX_DEPTH)return ASTER_ENOSPC;
 int r=place_children(s,parent,world.w,world.h);if(r)return r;
 aster_scene_node_t *p=parent?find(s,parent):NULL;
 int32_t scrollx=p?p->scroll_x:0,scrolly=p?p->scroll_y:0;
 bool have=false;int32_t last_z=0;uint32_t last_id=0;
 for(;;){int pick=-1;
  for(uint32_t i=0;i<s->capacity;i++){aster_scene_node_t *n=&s->nodes[i];if(!n->used||n->parent!=parent||!n->visible||!n->opacity)continue;
   if(have&&(n->z<last_z||(n->z==last_z&&n->id<=last_id)))continue;
   if(pick<0||n->z<s->nodes[pick].z||(n->z==s->nodes[pick].z&&n->id<s->nodes[pick].id))pick=(int)i;
  }
  if(pick<0)break;aster_scene_node_t *n=&s->nodes[pick];last_z=n->z;last_id=n->id;have=true;
  aster_box_t b=s->local[pick];b.x+=world.x-scrollx;b.y+=world.y-scrolly;
  s->world[pick]=b;s->clip[pick]=intersection(b,clip);
  if(!s->clip[pick].w)continue;s->work_order[s->work_count++]=(uint16_t)pick;
  if(n->kind==ASTER_NODE_CONTAINER){r=walk(s,n->id,b,s->clip[pick],depth+1);if(r)return r;}
 }return 0;
}
static int drain(aster_ge_t *g){return (g->n||g->dirty)?aster_ge_exec(g,0,0,0,0):0;}
static int clip_to(aster_ge_t *g,aster_box_t b){
 int r=drain(g);if(r<0)return r;
 aster_gfx_frame_clip_t c={sizeof(c),1,ASTER_GFX_FRAME_SET_CLIP,g->frame_id,b.x,b.y,b.w,b.h};
 return aster_gfx_exec(g->surf,NULL,ASTER_GFX_FRAME_CONTROL,(const uint8_t *)&c,sizeof(c),ASTER_GFX_FRAME_MAGIC,1,0,0);
}
int32_t aster_scene_render(aster_scene_t *s){
 if(!s||!s->ready)return ASTER_ESTATE;int r=aster_scene_poll(s);if(r<0)return r;
 bool changed=s->force_full||s->dirty;
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used&&s->nodes[i].changed)changed=true;
 if(!changed)return 0;
 aster_box_t screen={0,0,s->ge->w,s->ge->h};
 if(s->layout_dirty){
  memset(s->work_cw,0,sizeof(s->work_cw));memset(s->work_ch,0,sizeof(s->work_ch));
  memset(s->world,0,sizeof(s->world));memset(s->clip,0,sizeof(s->clip));s->work_count=0;
  r=walk(s,0,screen,screen,0);if(r)return r;
 }else{
  s->work_count=s->order_count;memcpy(s->work_order,s->order,s->order_count*sizeof(*s->order));
  for(uint32_t i=0;i<s->capacity;i++){
   s->world[i]=s->nodes[i].last_world;s->clip[i]=s->nodes[i].last_clip;
   s->work_cw[i]=s->nodes[i].content_w;s->work_ch[i]=s->nodes[i].content_h;
  }
 }
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used){aster_scene_node_t *n=&s->nodes[i];
  if(n->changed||memcmp(&n->last_world,&s->world[i],sizeof(aster_box_t))||memcmp(&n->last_clip,&s->clip[i],sizeof(aster_box_t))){damage(s,n->last_clip);damage(s,s->clip[i]);}
 }
 if(s->force_full||(s->flags&ASTER_SCENE_CANVAS))damage(s,screen);
 if(!s->dirty)goto committed;
 aster_ge_t *g=s->ge;if(g->frame_id||g->n||g->dirty||g->err)return ASTER_ESTATE;
 r=aster_ge_begin_frame(g);if(r<0)return r;
 if(s->flags&ASTER_SCENE_CANVAS){aster_ge_clear(g,s->background);r=drain(g);if(r<0)goto failed;}
 else{r=clip_to(g,s->damage);if(r<0)goto failed;aster_ge_rect(g,s->damage.x,s->damage.y,s->damage.w,s->damage.h,s->background);}
 for(uint32_t j=0;j<s->work_count;j++){
  uint32_t i=s->work_order[j];aster_scene_node_t *n=&s->nodes[i];aster_box_t b=s->world[i],c=intersection(s->clip[i],s->damage);
  if(!c.w)continue;
  if(n->kind==ASTER_NODE_CONTAINER&&!n->background)continue;
  if(n->kind==ASTER_NODE_IMAGE&&!n->image){r=ASTER_EBADF;goto failed;}
  r=clip_to(g,c);if(r<0)goto failed;
  if(n->kind==ASTER_NODE_RECT||n->kind==ASTER_NODE_CONTAINER)aster_ge_rect(g,c.x,c.y,c.w,c.h,n->color);
  else if(n->kind==ASTER_NODE_IMAGE)aster_ge_bmp(g,n->image,n->image_w,n->image_h,b.x,b.y,n->scale,n->opacity);
  else if(n->kind==ASTER_NODE_TEXT){
   int32_t x=b.x,y=b.y+(b.h-aster_text_line_h(n->px))/2+aster_text_asc(n->px);
   if(n->text_align==1)x+=(b.w-n->text_width)/2;else if(n->text_align==2)x+=b.w-n->text_width;
   if(x<-32768||x>32767||y<-32768||y>32767){r=ASTER_EINVAL;goto failed;}
   r=aster_ge_text(g,x,y,n->px,n->color,ASTER_GFX_NOBG,n->text);if(r<0)goto failed;
  }
 }
 r=aster_ge_present(g);if(r<0)goto failed;
committed:
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used){s->nodes[i].last_world=s->world[i];s->nodes[i].last_clip=s->clip[i];s->nodes[i].content_w=s->work_cw[i];s->nodes[i].content_h=s->work_ch[i];s->nodes[i].changed=false;}
 s->order_count=s->work_count;memcpy(s->order,s->work_order,s->order_count*sizeof(*s->order));
 changed=s->dirty;s->dirty=s->force_full=s->layout_dirty=false;s->damage=(aster_box_t){0};return changed?1:0;
failed:
 if(g->frame_id)(void)aster_ge_cancel_frame(g);s->force_full=true;return r;
}
aster_node_id_t aster_scene_hit_test(const aster_scene_t *s,int32_t x,int32_t y){
 if(!s||!s->ready)return 0;
 for(uint32_t j=s->order_count;j;j--){const aster_scene_node_t *n=&s->nodes[s->order[j-1]];if(n->used&&n->interactive&&inside(n->last_clip,x,y))return n->id;}return 0;
}
int32_t aster_scene_bounds(const aster_scene_t *s,uint32_t id,aster_box_t *out){
 if(!s||!s->ready||!out)return ASTER_EINVAL;
 for(uint32_t i=0;i<s->capacity;i++)if(s->nodes[i].used&&s->nodes[i].id==id){*out=s->nodes[i].last_clip;return 0;}return ASTER_EBADF;
}
