import subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CODE=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <aster_gfx_engine.h>
static int old,active,published,draws,cancels,fail;
int32_t aster_gfx_exec(aster_handle_t s,const aster_gfx_cmd_t *c,uint32_t n,const uint8_t *a,uint32_t len,uint32_t x,uint32_t y,uint32_t w,uint32_t h){
 (void)s;(void)c;(void)len;(void)w;(void)h;
 if(n==ASTER_GFX_FRAME_CONTROL){
  if(old)return ASTER_EINVAL;
  assert(x==ASTER_GFX_FRAME_MAGIC&&y==1);
  aster_gfx_frame_control_t *v=(aster_gfx_frame_control_t*)a;
  if(v->action==0){v->flags=1;return 0;}
  if(v->action==1){assert(!active);active=1;v->frame_id=7;return 0;}
  assert(v->frame_id==7&&active);active=0;
  if(v->action==2)published++;else cancels++;return 0;
 }
 draws++;if(fail&&draws==fail)return ASTER_EIO;
 if(!active)published++;return n;
}
int main(void){
 aster_ge_t g;aster_gfx_cmd_t ring[1];uint8_t arena[8];
 aster_ge_init(&g,1,4,4,ring,1,arena,8);
 old=1;assert(aster_ge_begin_frame(&g)==ASTER_ENOTSUP&&!g.frame_id&&draws==0);
 old=0;assert(aster_ge_begin_frame(&g)==0);
 for(int i=0;i<5;i++)aster_ge_rect(&g,0,0,1,1,0xffff);
 assert(draws==4&&published==0);
 assert(aster_ge_present(&g)==0&&published==1&&!active&&!g.frame_id);
 draws=0;fail=2;assert(aster_ge_begin_frame(&g)==0);
 for(int i=0;i<5;i++)aster_ge_rect(&g,0,0,1,1,0xffff);
 assert(aster_ge_present(&g)==ASTER_EIO&&published==1&&cancels==1&&!active);
 fail=0;assert(aster_ge_begin_frame(&g)==0);
 assert(aster_ge_text(&g,0,2,16,0xffff,ASTER_GFX_NOBG,"too much text")<0);
 assert(aster_ge_present(&g)<0&&cancels==2&&published==1);
 assert(aster_ge_begin_frame(&g)==0);aster_ge_clear(&g,0);assert(aster_ge_present(&g)==0&&published==2);
 return 0;
}
'''
class FrameEngine(unittest.TestCase):
 def test_probe_spill_failure_cancel_and_recovery(self):
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'test';r=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/aster'),'-x','c','-','-o',str(p)],input=CODE,text=True,capture_output=True)
   self.assertEqual(r.returncode,0,r.stderr);subprocess.run([str(p)],check=True,timeout=10)
