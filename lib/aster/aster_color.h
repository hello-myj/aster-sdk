/* SPDX-License-Identifier: MIT */
#ifndef ASTER_COLOR_CANVAS_H
#define ASTER_COLOR_CANVAS_H
#include "aster_rgb888.h"
/* One source for P4 and older RGB565 devices. Native RGB888 commands are
 * negotiated; otherwise rasterize the same 24-bit color description into the
 * existing RGB565 canvas, using the legacy native font service for text.
 * Immediate mode: legacy text may publish a sub-batch. No frame transaction is
 * claimed. Alpha text requires the native protocol; fallback returns ENOTSUP.
 */
static inline int aster_color_open(aster_rgb888_t *c,unsigned width,unsigned height,unsigned flags)
{
    int r=aster_rgb888_open(c,width,height,flags);if(r!=ASTER_ENOTSUP && r!=ASTER_EINVAL)return r;
    aster_surface_desc_t d={sizeof(d),1,width,height,ASTER_PIXEL_RGB565,flags};
    r=aster_surface_create(&d,sizeof(d),&c->surface);if(r)return r;
    r=aster_surface_acquire(c->surface,&c->buffer,sizeof(c->buffer));
    if(r){aster_surface_destroy(c->surface);*c=(aster_rgb888_t){0};}return r;
}
static inline unsigned aster_color_565(unsigned c)
{return ((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&31);}
static inline void aster_color_blend565(uint16_t *p,unsigned c,unsigned opa)
{
    if(opa!=255) {
        unsigned old=*p,r=((*p>>11)&31)*255/31,g=((old>>5)&63)*255/63,b=(old&31)*255/31;
        r=(((c>>16)&255)*opa+r*(255-opa)+127)/255;
        g=(((c>>8)&255)*opa+g*(255-opa)+127)/255;b=((c&255)*opa+b*(255-opa)+127)/255;c=(r<<16)|(g<<8)|b;
    }
    *p=aster_color_565(c);
}
static inline int aster_color_submit(const aster_rgb888_t *c,const aster_gfx_rgb888_cmd_t *commands,unsigned count,const void *arena,unsigned length)
{
    if(c->buffer.format!=ASTER_PIXEL_RGB565)return aster_rgb888_submit(c,commands,count,arena,length);
    if(!count || count>256 || length>ASTER_GFX_FRAME_MAX_ARENA || !commands || (length && !arena))return ASTER_EINVAL;
    for(unsigned i=0;i<count;i++) {
        const unsigned *w=commands[i].w;
        if(w[0]>ASTER_RGB888_TRI || w[7]>255 || (w[3]&0xff000000u))return ASTER_EINVAL;
        if(w[0]==ASTER_RGB888_TEXT) {
            if(w[7]!=255)return ASTER_ENOTSUP;
            if(w[2]>255 || w[5]>length || w[6]>length-w[5] || w[6]>65535 || (w[4]&0xfe000000u))return ASTER_EINVAL;
        }
        if(w[0]==ASTER_RGB888_GRAD && ((w[4]&0xff000000u) || w[6]>1))return ASTER_EINVAL;
        if(w[0]==ASTER_RGB888_RECT && (w[4]>65535 || w[6]>1024))return ASTER_EINVAL;
        if(w[0]==ASTER_RGB888_LINE && (!w[4] || w[4]>1024))return ASTER_EINVAL;
        if(w[0]==ASTER_RGB888_CIRCLE && (w[2]>32767 || w[4]>1024))return ASTER_EINVAL;
    }
    aster_gfx_cmd_t legacy[16];unsigned pending=0;int cpu_dirty=0;
    for(unsigned i=0;i<count;i++) {
        const unsigned *w=commands[i].w;unsigned op=w[0];
        /* Keep bulk 565 drawing in the existing native firmware rasterizer.
         * Only operations absent from that ABI (e.g. translucent geometry)
         * use bounded guest-side rasterization. Existing S3 fast paths stay in use. */
        if(w[7]==255 && (op!=ASTER_RGB888_RECT || w[6]<=1) &&
           (op!=ASTER_RGB888_CIRCLE || w[4]<=1) && (op!=ASTER_RGB888_LINE || w[4]==1)) {
            aster_gfx_cmd_t out={{0}};
            switch(op) {
            case ASTER_RGB888_CLEAR:out.w[0]=ASTER_GFX_CLEAR;out.w[1]=aster_color_565(w[3]);break;
            case ASTER_RGB888_RECT:out.w[0]=(w[4] || w[6])?ASTER_GFX_RECT_S:ASTER_GFX_RECT_F;out.w[1]=w[1];out.w[2]=w[2];out.w[3]=aster_color_565(w[3]);out.w[4]=w[4]|(w[6]?0x10000u:0);break;
            case ASTER_RGB888_GRAD:out.w[0]=ASTER_GFX_GRAD;out.w[1]=w[1];out.w[2]=w[2];out.w[3]=aster_color_565(w[3]);out.w[4]=aster_color_565(w[4]);out.w[5]=w[6];break;
            case ASTER_RGB888_TEXT:out.w[0]=ASTER_GFX_TEXT;out.w[1]=w[1];out.w[2]=ASTER_GFX_PACK(w[2],aster_color_565(w[3]));out.w[3]=w[5];out.w[4]=w[6];out.w[5]=aster_color_565(w[4])|((w[4]&0x1000000u)?0x10000u:0);break;
            case ASTER_RGB888_LINE:out.w[0]=ASTER_GFX_LINE_AA;out.w[1]=w[1];out.w[2]=w[2];out.w[3]=aster_color_565(w[3]);break;
            case ASTER_RGB888_CIRCLE:out.w[0]=ASTER_GFX_ELLIPSE;out.w[1]=w[1];out.w[2]=ASTER_GFX_PACK(w[2],w[2]);out.w[3]=aster_color_565(w[3])|(w[4]?0x10000u:0);break;
            case ASTER_RGB888_TRI:out.w[0]=ASTER_GFX_TRI;out.w[1]=w[1];out.w[2]=w[2];out.w[3]=w[4];out.w[4]=aster_color_565(w[3]);break;
            }
            legacy[pending++]=out;
            if(pending==16){int r=aster_gfx_exec(c->surface,legacy,pending,arena,length,0,0,0,0);if(r<0)return r;pending=0;}
            continue;
        }
        if(pending){int r=aster_gfx_exec(c->surface,legacy,pending,arena,length,0,0,0,0);if(r<0)return r;pending=0;}
        cpu_dirty=1;
        int x0=ASTER_GFX_P16(w[1]),y0=ASTER_GFX_P16H(w[1]);
        int x1=ASTER_GFX_P16(w[2]),y1=ASTER_GFX_P16H(w[2]);
        int x2=ASTER_GFX_P16(w[4]),y2=ASTER_GFX_P16H(w[4]);
        int wide=ASTER_GFX_U16(w[2]),high=ASTER_GFX_HI16(w[2]);
        if(op==ASTER_RGB888_TEXT) {
            aster_gfx_cmd_t text={.w={ASTER_GFX_TEXT,w[1],ASTER_GFX_PACK(w[2],aster_color_565(w[3])),w[5],w[6],aster_color_565(w[4])|((w[4]&0x1000000u)?0x10000:0)}};
            int r=aster_gfx_exec(c->surface,&text,1,arena,length,0,0,0,0);if(r<0)return r;continue;
        }
        int l=x0,t=y0,r=x0+wide,b=y0+high;
        if(op==ASTER_RGB888_CLEAR){l=t=0;r=c->buffer.width;b=c->buffer.height;}
        if(op==ASTER_RGB888_CIRCLE){l=x0-(int)w[2];r=x0+w[2]+1;t=y0-(int)w[2];b=y0+w[2]+1;}
        if(op==ASTER_RGB888_LINE){int pad=w[4]/2+1;l=(x0<x1?x0:x1)-pad;r=(x0>x1?x0:x1)+pad+1;t=(y0<y1?y0:y1)-pad;b=(y0>y1?y0:y1)+pad+1;}
        if(op==ASTER_RGB888_TRI){l=x0<x1?x0:x1;if(x2<l)l=x2;r=x0>x1?x0:x1;if(x2>r)r=x2;r++;t=y0<y1?y0:y1;if(y2<t)t=y2;b=y0>y1?y0:y1;if(y2>b)b=y2;b++;}
        if(l<0)l=0;if(t<0)t=0;if(r>(int)c->buffer.width)r=c->buffer.width;if(b>(int)c->buffer.height)b=c->buffer.height;
        for(int y=t;y<b;y++)for(int x=l;x<r;x++) {
            unsigned color=w[3];int inside=1;
            if(op==ASTER_RGB888_RECT) {
                int radius=w[4];if(radius>wide/2)radius=wide/2;if(radius>high/2)radius=high/2;
                int dx=x-x0,dy=y-y0;
                if(radius) {int ax=dx<radius?radius-dx:dx>=wide-radius?dx-wide+radius+1:0;int ay=dy<radius?radius-dy:dy>=high-radius?dy-high+radius+1:0;if(ax*ax+ay*ay>radius*radius)inside=0;}
                if(w[6] && dx>=(int)w[6] && dy>=(int)w[6] && dx<wide-(int)w[6] && dy<high-(int)w[6])inside=0;
            }
            if(op==ASTER_RGB888_GRAD) {
                int pos=w[6]?x-x0:y-y0,span=(w[6]?wide:high)-1;if(span<1)span=1;color=0;
                for(unsigned shift=0;shift<24;shift+=8){int a=(w[3]>>shift)&255,z=(w[4]>>shift)&255;color|=(unsigned)(a+(z-a)*pos/span)<<shift;}
            }
            if(op==ASTER_RGB888_CIRCLE){int64_t dx=x-x0,dy=y-y0,rad=w[2],inner=rad-w[4];inside=dx*dx+dy*dy<=rad*rad;if(w[4] && inner>0 && dx*dx+dy*dy<inner*inner)inside=0;}
            if(op==ASTER_RGB888_LINE){float dx=x1-x0,dy=y1-y0,len=dx*dx+dy*dy,u=len?((x-x0)*dx+(y-y0)*dy)/len:0;if(u<0)u=0;if(u>1)u=1;dx=x-(x0+u*dx);dy=y-(y0+u*dy);float radius=w[4]*0.5f;inside=dx*dx+dy*dy<=radius*radius;}
            if(op==ASTER_RGB888_TRI){int64_t a=(int64_t)(x1-x0)*(y-y0)-(int64_t)(y1-y0)*(x-x0),bb=(int64_t)(x2-x1)*(y-y1)-(int64_t)(y2-y1)*(x-x1),d=(int64_t)(x0-x2)*(y-y2)-(int64_t)(y0-y2)*(x-x2);inside=(a>=0 && bb>=0 && d>=0)||(a<=0 && bb<=0 && d<=0);}
            if(inside)aster_color_blend565((uint16_t *)((uint8_t *)c->buffer.ptr+(size_t)y*c->buffer.pitch)+x,color,w[7]);
        }
    }
    if(pending){int r=aster_gfx_exec(c->surface,legacy,pending,arena,length,0,0,0,0);if(r<0)return r;}
    if(cpu_dirty){int r=aster_surface_present(c->surface);if(r)return r;}
    return (int)count;
}
static inline void aster_color_pixel(const aster_rgb888_t *c,unsigned x,unsigned y,unsigned color)
{
    if(x>=c->buffer.width || y>=c->buffer.height)return;
    if(c->buffer.format==ASTER_PIXEL_RGB565)aster_color_blend565((uint16_t *)((uint8_t *)c->buffer.ptr+(size_t)y*c->buffer.pitch)+x,color,255);
    else aster_rgb888_pixel(c,x,y,color);
}
/* Source is packed R,G,B; destination format is selected at runtime. */
static inline int aster_color_blit_rgb888(const aster_rgb888_t *c,int x,int y,const uint8_t *source,unsigned w,unsigned h,unsigned pitch)
{
    if(!source || pitch<w || (uint64_t)pitch*h*3>UINT32_MAX)return ASTER_EINVAL;
    unsigned start_x=x<0?(unsigned)(-(int64_t)x):0,start_y=y<0?(unsigned)(-(int64_t)y):0;
    for(unsigned row=start_y;row<h && (int64_t)y+row<c->buffer.height;row++)
        for(unsigned col=start_x;col<w && (int64_t)x+col<c->buffer.width;col++) {
            const uint8_t *p=source+((size_t)row*pitch+col)*3;aster_color_pixel(c,x+col,y+row,((unsigned)p[0]<<16)|((unsigned)p[1]<<8)|p[2]);
        }
    return 0;
}
#endif
