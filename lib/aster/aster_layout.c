/* SPDX-License-Identifier: MIT */
#include "aster_layout.h"
int32_t aster_layout_compute(const aster_layout_t *l,int32_t w,int32_t h,
 const aster_layout_item_t *in,uint32_t n,aster_box_t *out,int32_t *cw,int32_t *ch)
{
 if(!l||n>ASTER_SCENE_LIMIT||(!in&&n)||(!out&&n)||!cw||!ch||w<0||h<0||w>32767||h>32767||
    l->mode<ASTER_LAYOUT_ROW||l->mode>ASTER_LAYOUT_GRID||l->align>ASTER_ALIGN_STRETCH||l->scroll>1) return ASTER_EINVAL;
 int32_t p=l->padding,g=l->gap,iw=w-2*p,ih=h-2*p;
 if(iw<0||ih<0) return ASTER_ENOSPC;
 aster_box_t temp[ASTER_SCENE_LIMIT];int32_t usedw=2*p,usedh=2*p;
 for(uint32_t i=0;i<n;i++)if(in[i].w<0||in[i].h<0||in[i].w>32767||in[i].h>32767)return ASTER_EINVAL;
 if(l->mode==ASTER_LAYOUT_GRID) {
  uint32_t cols=l->columns;if(!cols||cols>ASTER_SCENE_LIMIT)return ASTER_EINVAL;
  int32_t space=iw-(int32_t)(cols-1)*g;if(space<0)return ASTER_ENOSPC;
  int32_t cell=space/(int32_t)cols,rem=space%(int32_t)cols,y=p;
  for(uint32_t first=0;first<n;first+=cols){
   uint32_t end=first+cols<n?first+cols:n;int32_t rh=0;
   for(uint32_t i=first;i<end;i++)if(in[i].h>rh)rh=in[i].h;
   int32_t x=p;
   for(uint32_t i=first;i<end;i++){
    int32_t width=cell+((int32_t)(i-first)<rem),want=in[i].w?in[i].w:width;
    if(want>width)return ASTER_ENOSPC;
    int32_t height=in[i].h?in[i].h:rh,dx=0,dy=0;
    if(l->align==ASTER_ALIGN_STRETCH){want=width;height=rh;}
    else if(l->align==ASTER_ALIGN_CENTER){dx=(width-want)/2;dy=(rh-height)/2;}
    else if(l->align==ASTER_ALIGN_END){dx=width-want;dy=rh-height;}
    temp[i]=(aster_box_t){x+dx,y+dy,want,height};x+=width+g;
   }
   y+=rh;if(end<n)y+=g;
   if(y+p>32767)return ASTER_ENOSPC;
  }
  usedw=w;usedh=y+p;
  if(!l->scroll&&usedh>h)return ASTER_ENOSPC;
 }else{
  bool row=l->mode==ASTER_LAYOUT_ROW;int32_t main=row?iw:ih,cross=row?ih:iw;
  int64_t fixed=n?(int64_t)(n-1)*g:0;uint32_t weights=0;
  for(uint32_t i=0;i<n;i++){fixed+=row?in[i].w:in[i].h;weights+=in[i].grow;}
  if(fixed>32767-2*p||(!l->scroll&&fixed>main))return ASTER_ENOSPC;
  int32_t extra=main>fixed?main-(int32_t)fixed:0,pos=p;uint32_t before=0;
  for(uint32_t i=0;i<n;i++){
   int32_t len=row?in[i].w:in[i].h,side=row?in[i].h:in[i].w;
   if(weights&&in[i].grow){uint32_t next=before+in[i].grow;len+=(int64_t)extra*next/weights-(int64_t)extra*before/weights;before=next;}
   if(side>cross)return ASTER_ENOSPC;
   int32_t off=0;if(!side||l->align==ASTER_ALIGN_STRETCH)side=cross;
   else if(l->align==ASTER_ALIGN_CENTER)off=(cross-side)/2;
   else if(l->align==ASTER_ALIGN_END)off=cross-side;
   temp[i]=row?(aster_box_t){pos,p+off,len,side}:(aster_box_t){p+off,pos,side,len};
   pos+=len;if(i+1<n)pos+=g;
  }
  if(pos+p>32767)return ASTER_ENOSPC;
  if(row){usedw=pos+p;usedh=h;}else{usedw=w;usedh=pos+p;}
 }
 for(uint32_t i=0;i<n;i++)out[i]=temp[i];*cw=usedw;*ch=usedh;return 0;
}
