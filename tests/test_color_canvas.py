import shutil,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CODE=r'''
#include <assert.h>
#include <string.h>
#include <aster_color.h>
static unsigned native,creates,presents,batches,legacy_calls;
static uint8_t pixels[64*64*3];
int32_t aster_display_get_info(uint32_t i,aster_display_info_t *p,uint32_t n) {
 (void)i;(void)n;p->native_format=native?ASTER_PIXEL_BGR888:ASTER_PIXEL_RGB565;return 0;
}
int32_t aster_surface_create(const aster_surface_desc_t *d,uint32_t n,aster_handle_t *p) {
 (void)n;assert(d->format==(native?ASTER_PIXEL_BGR888:ASTER_PIXEL_RGB565));creates++;*p=7;return 0;
}
int32_t aster_surface_acquire(aster_handle_t h,aster_surface_buffer_t *b,uint32_t n) {
 (void)n;assert(h==7);*b=(aster_surface_buffer_t){.size=sizeof(*b),.version=1,.ptr=pixels,.width=64,.height=64,.pitch=64*(native?3:2),.format=native?ASTER_PIXEL_BGR888:ASTER_PIXEL_RGB565};return 0;
}
int32_t aster_surface_destroy(aster_handle_t h){assert(h==7);return 0;}
int32_t aster_surface_present(aster_handle_t h){assert(h==7);presents++;return 0;}
int32_t aster_gfx_exec(aster_handle_t h,const aster_gfx_cmd_t *cmd,unsigned n,const uint8_t *a,unsigned len,unsigned x,unsigned y,unsigned w,unsigned hh){
 assert(h==7);(void)cmd;(void)len;(void)w;(void)hh;
 if(n==ASTER_GFX_FRAME_CONTROL){assert(native && x==ASTER_GFX_FRAME_MAGIC && y==1);((aster_gfx_frame_control_t *)a)->flags=ASTER_GFX_RGB888_SUPPORTED;return 0;}
 if(n==ASTER_GFX_RGB888_BATCH){assert(native && x==2 && y==1);batches++;return x;}
 assert(!native && n<=16);legacy_calls++;
 for(unsigned i=0;i<n;i++) {
  const unsigned *v=cmd[i].w;
  if(v[0]==ASTER_GFX_CLEAR)for(unsigned j=0;j<64*64;j++)((uint16_t *)pixels)[j]=v[1];
  if(v[0]==ASTER_GFX_GRAD)for(unsigned yy=0;yy<4;yy++)for(unsigned xx=0;xx<64;xx++)((uint16_t *)pixels)[yy*64+xx]=(xx*31/63)<<11;
 }
 return n;
}
int main(void){
 aster_rgb888_t c;native=0;assert(aster_color_open(&c,0,0,0)==0 && creates==1 && c.buffer.format==ASTER_PIXEL_RGB565);
 aster_gfx_rgb888_cmd_t commands[]={aster_rgb888_clear(0),aster_rgb888_gradient(0,0,64,4,0,0xff0000,1)};
 assert(aster_color_submit(&c,commands,2,0,0)==2 && presents==0 && batches==0);
 unsigned distinct=1,last=((uint16_t *)pixels)[0]>>11;
 for(unsigned x=1;x<64;x++){unsigned v=((uint16_t *)pixels)[x]>>11;if(v!=last){last=v;distinct++;}}
 assert(distinct==32);
 aster_color_pixel(&c,0,0,0xabcdef);assert(((uint16_t *)pixels)[0]==aster_color_565(0xabcdef));
 uint8_t rgb[]={0xff,0,0,0,0xff,0};assert(!aster_color_blit_rgb888(&c,-1,3,rgb,2,1,2));assert(((uint16_t *)pixels)[3*64]==0x07e0);
 uint8_t before[sizeof(pixels)];memcpy(before,pixels,sizeof(pixels));commands[1].w[0]=99;
 assert(aster_color_submit(&c,commands,2,0,0)==ASTER_EINVAL && !memcmp(before,pixels,sizeof(pixels)));
 aster_gfx_rgb888_cmd_t many[17];for(unsigned i=0;i<17;i++)many[i]=aster_rgb888_clear(i==16?0x777777:0);
 unsigned previous=legacy_calls;assert(aster_color_submit(&c,many,17,0,0)==17 && legacy_calls==previous+2);
 assert(((uint16_t *)pixels)[0]==aster_color_565(0x777777));
 aster_gfx_rgb888_cmd_t alpha=aster_rgb888_rect(0,0,1,1,0xff0000,0,0);alpha.w[7]=128;
 assert(aster_color_submit(&c,&alpha,1,0,0)==1 && presents==1);
 assert(((uint16_t *)pixels)[0]!=aster_color_565(0x777777) && ((uint16_t *)pixels)[0]!=0xf800);
 assert(!aster_rgb888_close(&c));native=1;assert(!aster_color_open(&c,0,0,0) && creates==2);
 commands[1]=aster_rgb888_gradient(0,0,64,4,0,0xff0000,1);assert(aster_color_submit(&c,commands,2,0,0)==2 && batches==1);
 aster_color_pixel(&c,0,0,0x070503);assert(pixels[0]==3 && pixels[1]==5 && pixels[2]==7);
 return 0;
}
'''
class AdaptiveCanvas(unittest.TestCase):
 def test_native_and_rgb565_backend(self):
  cc=shutil.which("cc") or shutil.which("gcc")
  if not cc:self.skipTest("C compiler unavailable")
  with tempfile.TemporaryDirectory() as td:
   src=Path(td)/"test.c";exe=Path(td)/"test";src.write_text(CODE)
   subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Wno-misleading-indentation","-I"+str(ROOT/"include"),"-I"+str(ROOT/"lib/aster"),str(src),"-o",str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
