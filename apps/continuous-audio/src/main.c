/* SPDX-License-Identifier: MIT. No third-party audio assets. */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
static const char path[]="/data/loop.wav";
static aster_handle_t surface;
static int32_t effect;
static int running, fast;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[16];
static uint8_t arena[256];
static void le16(uint8_t *p,uint16_t v) {p[0]=v;p[1]=v>>8;}
static void le32(uint8_t *p,uint32_t v) {le16(p,(uint16_t)v);le16(p+2,(uint16_t)(v>>16));}
static int write_wav(void)
{
 uint8_t header[44]={0},block[256];aster_handle_t file=0;
 memcpy(header,"RIFF",4);le32(header+4,36+4096);memcpy(header+8,"WAVEfmt ",8);
 le32(header+16,16);le16(header+20,1);le16(header+22,1);
 le32(header+24,16000);le32(header+28,32000);le16(header+32,2);le16(header+34,16);
 memcpy(header+36,"data",4);le32(header+40,4096);
 int rc=aster_fs_open(path,sizeof(path)-1,ASTER_FS_O_CREAT|ASTER_FS_O_TRUNC|ASTER_FS_O_WRONLY,&file);
 if(rc)return rc;
 if(aster_fs_write(file,header,sizeof(header))!=sizeof(header))rc=ASTER_EIO;
 for(unsigned i=0;i<128;i++) {unsigned phase=i%16;int v=phase<8?(int)phase*4000-16000:48000-(int)phase*4000;le16(block+i*2,(uint16_t)v);}
 for(unsigned i=0;i<16 && !rc;i++)if(aster_fs_write(file,block,sizeof(block))!=sizeof(block))rc=ASTER_EIO;
 int close_rc=aster_fs_close(file);return rc?rc:close_rc;
}
static int play(void)
{
 aster_audio_effect_play_desc_t d={.size=sizeof(d),.version=1,
  .flags=ASTER_AUDIO_EFFECT_F_LOOP,.pitch_q16=fast?98304:65536,
  .gain_q16=32768,.fade_in_ms=80};
 int rc=aster_audio_effect_play_ex((uint32_t)effect,&d,sizeof(d));
 if(!rc)running=1;return rc;
}
int aster_app_init(void)
{
 if(!aster_system_has_feature(ASTER_FEATURE_AUDIO_EFFECTS))return ASTER_ENODEV;
 int rc=write_wav();if(rc)return rc;
 effect=aster_audio_effect_load(path,sizeof(path)-1);if(effect<0)return effect;
 aster_surface_desc_t d={sizeof(d),1,0,0,ASTER_PIXEL_RGB565,ASTER_SURFACE_F_PANEL_CANVAS};
 aster_surface_buffer_t b={.size=sizeof(b),.version=1};
 rc=aster_surface_create(&d,sizeof(d),&surface);if(rc)return rc;
 rc=aster_surface_acquire(surface,&b,sizeof(b));if(rc)return rc;
 aster_ge_init(&ge,surface,b.width,b.height,commands,16,arena,sizeof(arena));
 aster_app_set_framerate(5);return ASTER_OK;
}
void aster_app_start(void) {(void)play();}
void aster_app_event(const aster_event_t *e)
{
 if(e->type==ASTER_EVENT_TOUCH && e->u.touch.action==ASTER_TOUCH_DOWN && e->u.touch.slot==0) {
  int rc;
  if(e->u.touch.x<ge.w/2) {
   fast=!fast;
   aster_audio_effect_params_t p={sizeof(p),1,fast?98304:65536,32768,120};
   rc=aster_audio_effect_set_params((uint32_t)effect,&p,sizeof(p));
  } else if(running) {rc=aster_audio_effect_stop((uint32_t)effect,100);if(!rc)running=0;}
  else rc=play();
  if(rc)aster_logf(ASTER_LOG_ERROR,"effect control failed: %d",rc);
 }
 if(e->type!=ASTER_EVENT_FRAME)return;
 aster_ge_clear(&ge,ASTER_RGB565(14,22,34));
 aster_ge_text(&ge,12,36,20,ASTER_WHITE,0,"Continuous Audio");
 aster_ge_text(&ge,12,70,14,ASTER_CYAN,0,"5 FPS / worker-driven loop");
 aster_ge_text(&ge,12,104,14,ASTER_WHITE,0,fast?"Pitch: 1.5x":"Pitch: 1.0x");
 aster_ge_text(&ge,12,134,14,ASTER_WHITE,0,running?"Playing":"Stopped (PCM cached)");
 aster_ge_text(&ge,12,174,12,ASTER_WHITE,0,"Left: pitch / Right: stop or play");
 aster_ge_rect(&ge,(aster_time_now_ms()/20)%(ge.w-24),210,24,24,ASTER_GREEN);
 (void)aster_ge_present(&ge);
}
void aster_app_pause(void) {} /* Runtime freezes effect phase automatically. */
void aster_app_resume(void) {}
void aster_app_stop(void) {if(effect>0)(void)aster_audio_effect_stop((uint32_t)effect,0);}
void aster_app_destroy(void)
{
 if(effect>0){(void)aster_audio_effect_unload((uint32_t)effect);effect=0;}
 if(surface){(void)aster_surface_destroy(surface);surface=0;}
}
