/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include "aster_color.h"
static aster_rgb888_t canvas;
static struct {unsigned x,y;int down;} points[5];
static int dirty=1;
static void render(void)
{
    static const char text[]="Adaptive color canvas\nP4 RGB666 / lckfb RGB565";
    aster_gfx_rgb888_cmd_t c[24];unsigned n=0;
    unsigned width=canvas.buffer.width,height=canvas.buffer.height,step=(height-160)/4;
    c[n++]=aster_rgb888_clear(0x10131b);
    c[n++]=aster_rgb888_text(16,30,24,0xe8efff,0,strlen(text));
    for(unsigned row=0;row<4;row++) {
        uint32_t end=row==0?0xff0000:row==1?0x00ff00:row==2?0x0000ff:0xffffff;
        c[n++]=aster_rgb888_gradient(16,80+row*step,width-32,step-12,0,end,1);
    }
    c[n++]=aster_rgb888_rect(16,height-68,width-32,52,0x2b7cb9,18,0);c[n-1].w[7]=128;
    c[n++]=aster_rgb888_circle(width-64,height-42,22,0x57d9c3,0);
    c[n++]=aster_rgb888_line(40,height-28,width-120,height-60,0xfa764b,2);
    static const unsigned colors[]={0xff4d72,0x48e1ad,0x57a5ff,0xffd45b,0xc385ff};
    for(unsigned i=0;i<5;i++)if(points[i].down)c[n++]=aster_rgb888_circle(points[i].x,points[i].y,18,colors[i],2);
    int r=aster_color_submit(&canvas,c,n,text,strlen(text));
    if(r<0){aster_printf("RGB888 drawing failed: %d\n",r);aster_app_exit(r);}
}
int aster_app_init(void)
{
    int r=aster_color_open(&canvas,0,0,0);if(r)return r;
    aster_printf("Adaptive color canvas: %ux%u pitch=%u format=%x\n",canvas.buffer.width,canvas.buffer.height,canvas.buffer.pitch,canvas.buffer.format);
    return 0;
}
void aster_app_start(void){render();dirty=0;}
void aster_app_event(const aster_event_t *event)
{
    if(event->type==ASTER_EVENT_TOUCH && event->u.touch.slot<5) {
        unsigned i=event->u.touch.slot;points[i].x=event->u.touch.x;points[i].y=event->u.touch.y;
        points[i].down=event->u.touch.action!=ASTER_TOUCH_UP;dirty=1;
    }
    if(event->type==ASTER_EVENT_FRAME && (dirty || aster_gfx_fonts_pending()>0)){render();dirty=0;}
}
void aster_app_stop(void){}
void aster_app_destroy(void){if(canvas.surface)aster_rgb888_close(&canvas);}
