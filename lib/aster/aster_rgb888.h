/* SPDX-License-Identifier: MIT */
#ifndef ASTER_RGB888_HELPERS_H
#define ASTER_RGB888_HELPERS_H
#include <aster/aster.h>
#include <aster/gfx_rgb888.h>
#include <stddef.h>
/* Optional high-color canvas. Existing aster_gfx/aster_canvas APIs remain
 * RGB565. This helper explicitly negotiates RGB888 commands; ENOTSUP on an
 * older host is never silently converted into lower-precision drawing. */
typedef struct {aster_handle_t surface;aster_surface_buffer_t buffer;} aster_rgb888_t;
static inline int aster_rgb888_open(aster_rgb888_t *out,uint32_t width,uint32_t height,uint32_t flags)
{
    *out=(aster_rgb888_t){0};aster_display_info_t info={0};
    int r=aster_display_get_info(0,&info,sizeof(info));if(r)return r;
    if(info.native_format!=ASTER_PIXEL_RGB888 && info.native_format!=ASTER_PIXEL_BGR888)return ASTER_ENOTSUP;
    aster_surface_desc_t desc={sizeof(desc),1,width,height,info.native_format,flags};
    r=aster_surface_create(&desc,sizeof(desc),&out->surface);if(r)return r;
    aster_gfx_frame_control_t q={sizeof(q),1,ASTER_GFX_FRAME_QUERY,0,0,0};
    r=aster_gfx_exec(out->surface,NULL,ASTER_GFX_FRAME_CONTROL,(const uint8_t *)&q,sizeof(q),ASTER_GFX_FRAME_MAGIC,1,0,0);
    if(!r && !(q.flags&ASTER_GFX_RGB888_SUPPORTED))r=ASTER_ENOTSUP;
    if(!r)r=aster_surface_acquire(out->surface,&out->buffer,sizeof(out->buffer));
    if(r){aster_surface_destroy(out->surface);*out=(aster_rgb888_t){0};}return r;
}
static inline int aster_rgb888_close(aster_rgb888_t *c)
{int r=aster_surface_destroy(c->surface);*c=(aster_rgb888_t){0};return r;}
static inline int aster_rgb888_submit(const aster_rgb888_t *c,const aster_gfx_rgb888_cmd_t *commands,uint32_t count,const void *arena,uint32_t length)
{return aster_gfx_exec(c->surface,(const aster_gfx_cmd_t *)commands,ASTER_GFX_RGB888_BATCH,arena,length,count,1,0,0);}
static inline int aster_rgb888_present(const aster_rgb888_t *c) {return aster_surface_present(c->surface);}
static inline void aster_rgb888_pixel(const aster_rgb888_t *c,uint32_t x,uint32_t y,uint32_t color)
{
    if(x>=c->buffer.width || y>=c->buffer.height)return;
    uint8_t *p=(uint8_t *)c->buffer.ptr+(size_t)y*c->buffer.pitch+x*3;
    if(c->buffer.format==ASTER_PIXEL_RGB888){p[0]=color>>16;p[1]=color>>8;p[2]=color;}
    else {p[0]=color;p[1]=color>>8;p[2]=color>>16;}
}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_cmd(uint32_t op,uint32_t color)
{aster_gfx_rgb888_cmd_t c={{0}};c.w[0]=op;c.w[3]=color;c.w[7]=255;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_clear(uint32_t color) {return aster_rgb888_cmd(ASTER_RGB888_CLEAR,color);}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_rect(int x,int y,unsigned w,unsigned h,uint32_t color,unsigned radius,unsigned border)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_cmd(ASTER_RGB888_RECT,color);c.w[1]=ASTER_GFX_PACK(x,y);c.w[2]=ASTER_GFX_PACK(w,h);c.w[4]=radius;c.w[6]=border;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_gradient(int x,int y,unsigned w,unsigned h,uint32_t c0,uint32_t c1,int horizontal)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_rect(x,y,w,h,c0,0,0);c.w[0]=ASTER_RGB888_GRAD;c.w[4]=c1;c.w[6]=horizontal?1:0;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_text(int x,int baseline,unsigned px,uint32_t color,unsigned offset,unsigned length)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_cmd(ASTER_RGB888_TEXT,color);c.w[1]=ASTER_GFX_PACK(x,baseline);c.w[2]=px;c.w[5]=offset;c.w[6]=length;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_line(int x0,int y0,int x1,int y1,uint32_t color,unsigned width)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_cmd(ASTER_RGB888_LINE,color);c.w[1]=ASTER_GFX_PACK(x0,y0);c.w[2]=ASTER_GFX_PACK(x1,y1);c.w[4]=width;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_circle(int x,int y,unsigned radius,uint32_t color,unsigned border)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_cmd(ASTER_RGB888_CIRCLE,color);c.w[1]=ASTER_GFX_PACK(x,y);c.w[2]=radius;c.w[4]=border;return c;}
static inline aster_gfx_rgb888_cmd_t aster_rgb888_triangle(int x0,int y0,int x1,int y1,int x2,int y2,uint32_t color)
{aster_gfx_rgb888_cmd_t c=aster_rgb888_cmd(ASTER_RGB888_TRI,color);c.w[1]=ASTER_GFX_PACK(x0,y0);c.w[2]=ASTER_GFX_PACK(x1,y1);c.w[4]=ASTER_GFX_PACK(x2,y2);return c;}
#endif
