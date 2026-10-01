/* SPDX-License-Identifier: MIT */
#include <aster_imports.h>
#include <aster_gfx_engine.h>
#include <aster_imu_filter.h>

static aster_handle_t surface;
static aster_ge_t ge;
static aster_gfx_cmd_t commands[128];
static uint8_t arena[2048];
static uint32_t width, height;
static aster_imu_pose_t pose;
static aster_imu_sample_t sample;
static float reference[4] = {1,0,0,0}, bias_sum[3];
static uint32_t calibration_count;
static int calibrating = 1, status, touch_target;
static uint32_t last_log;
static const float vertices[8][3] = {
    {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
    {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
};
static const uint8_t faces[6][4] = {
    {0,1,2,3},{4,7,6,5},{0,4,5,1},{3,2,6,7},{0,3,7,4},{1,5,6,2}
};
static const uint16_t colors[6] = {
    ASTER_RGB565(36,122,204),ASTER_RGB565(34,175,151),
    ASTER_RGB565(240,143,55),ASTER_RGB565(121,90,211),
    ASTER_RGB565(214,67,102),ASTER_RGB565(74,157,90)
};

static void release(void)
{
    if (surface) { aster_surface_destroy(surface); surface=0; }
}

static void calibrate_start(void)
{
    calibration_count=0; calibrating=1;
    memset(bias_sum,0,sizeof(bias_sum));
    memset(pose.bias,0,sizeof(pose.bias));
}

static void calibration(const aster_imu_sample_t *s)
{
    float a2=0, g2=0;
    for (int i=0;i<3;i++) { a2+=s->accel[i]*s->accel[i]; g2+=s->gyro[i]*s->gyro[i]; }
    /* Two seconds of stationary samples at 30 Hz, never average motion. */
    if (a2 < 75 || a2 > 120 || g2 > 0.0225f) {
        calibration_count=0; memset(bias_sum,0,sizeof(bias_sum)); return;
    }
    for (int i=0;i<3;i++) bias_sum[i]+=s->gyro[i];
    if (++calibration_count >= 60) {
        for (int i=0;i<3;i++) pose.bias[i]=bias_sum[i]/60.0f;
        memcpy(reference,pose.q,sizeof(reference));
        calibrating=0;
        aster_printf("imu calibrated bias urad/s %d %d %d",
            (int)(pose.bias[0]*1000000),(int)(pose.bias[1]*1000000),(int)(pose.bias[2]*1000000));
    }
}

static void text(int x,int top,uint32_t px,uint16_t color,const char *s)
{
    aster_ge_text(&ge,x,top+aster_text_asc((int)px),px,color,ASTER_GFX_NOBG,s);
}

static void cube_visual_quat(float q[4])
{
    aster_imu_pose_relative(&pose,reference,q);
    /* Sensor -> visual axes: conjugate by a 180-degree rotation about Z.
     * This reverses roll/pitch while preserving yaw, including combined
     * rotations. Keep this display mapping out of raw samples and fusion. */
    q[1]=-q[1];
    q[2]=-q[2];
}

static void render_cube(void)
{
    float q[4], view[8][3];
    int32_t sx[8], sy[8];
    float depth[6];
    uint8_t order[6]={0,1,2,3,4,5};
    cube_visual_quat(q);
    float radius=(float)(height-190)*0.28f;
    if (radius > (float)width*0.225f) radius=(float)width*0.225f;
    int cy=(int)height/2-17;
    for (int i=0;i<8;i++) {
        /* Identity relative pose faces the screen directly after zeroing. */
        aster_imu_quat_rotate(q,vertices[i],view[i]);
        float scale=radius*4.8f/(4.8f+view[i][2]);
        sx[i]=(int32_t)((float)width*0.5f+view[i][0]*scale);
        sy[i]=(int32_t)((float)cy-view[i][1]*scale);
    }
    for (int f=0;f<6;f++) {
        depth[f]=0;
        for (int j=0;j<4;j++) depth[f]+=view[faces[f][j]][2];
    }
    /* Painter's order: convex cube faces, far to near. */
    for (int i=0;i<5;i++) for (int j=i+1;j<6;j++) {
        if (depth[order[i]] < depth[order[j]]) { uint8_t t=order[i];order[i]=order[j];order[j]=t; }
    }
    for (int i=0;i<6;i++) {
        int f=order[i];const uint8_t *v=faces[f];
        aster_ge_tri(&ge,sx[v[0]],sy[v[0]],sx[v[1]],sy[v[1]],sx[v[2]],sy[v[2]],colors[f]);
        aster_ge_tri(&ge,sx[v[0]],sy[v[0]],sx[v[2]],sy[v[2]],sx[v[3]],sy[v[3]],colors[f]);
        for (int j=0;j<4;j++) {
            int k=(j+1)&3;
            aster_ge_line_aa(&ge,sx[v[j]],sy[v[j]],sx[v[k]],sy[v[k]],ASTER_RGB565(210,226,241));
        }
    }
}

static int32_t render(void)
{
    char value[64];
    uint16_t muted=ASTER_RGB565(146,164,184);
    aster_ge_clear(&ge,ASTER_RGB565(12,19,30));
    text(14,12,20,ASTER_WHITE,"IMU 立方体");
    if (status < 0) {
        text(14,44,16,ASTER_RED,"传感器读取失败，重试中");
    } else if (calibrating) {
        aster_snprintf(value,sizeof(value),"请静置校准 %u%%",calibration_count*100/60);
        text(14,44,16,muted,value);
    } else {
        text(14,44,16,muted,"转动设备，立方体随动");
    }
    render_cube();
    aster_snprintf(value,sizeof(value),"A %d %d %d mm/s2",
        (int)(sample.accel[0]*1000),(int)(sample.accel[1]*1000),(int)(sample.accel[2]*1000));
    text(14,(int)height-102,16,muted,value);
    aster_snprintf(value,sizeof(value),"G %d %d %d mrad/s",
        (int)(sample.gyro[0]*1000),(int)(sample.gyro[1]*1000),(int)(sample.gyro[2]*1000));
    text(14,(int)height-79,16,muted,value);
    int bw=((int)width-40)/2, by=(int)height-48;
    aster_ge_rect_r(&ge,14,by,bw,36,ASTER_RGB565(35,112,192),9,0);
    aster_ge_rect_r(&ge,26+bw,by,bw,36,ASTER_RGB565(35,49,65),9,0);
    text(14+12,by+7,16,ASTER_WHITE,"归零");
    text(26+bw+12,by+7,16,ASTER_WHITE,"校准");
    return aster_ge_present(&ge);
}

static int hit_button(uint32_t x,uint32_t y)
{
    if (y < height-48 || y >= height-12) return 0;
    uint32_t bw=(width-40)/2;
    if (x >= 14 && x < 14+bw) return 1;
    if (x >= 26+bw && x < 26+2*bw) return 2;
    return 0;
}

ASTER_APP_EXPORT int aster_app_init(void)
{
    aster_imu_info_t imu={.size=sizeof(imu),.version=1};
    int32_t rc=aster_imu_get_info(&imu,sizeof(imu));
    if (rc < 0) { aster_errfmt("imu info failed %d",rc);return rc; }
    aster_display_info_t display={.size=sizeof(display),.version=1};
    rc=aster_display_get_info(0,&display,sizeof(display));
    if (rc < 0) return rc;
    width=display.app_width;height=display.app_height;
    if (width < 200 || height < 300) return ASTER_ENOTSUP;
    aster_surface_desc_t desc={.size=sizeof(desc),.version=1,.width=width,.height=height,
        .format=ASTER_PIXEL_RGB565,.flags=ASTER_SURFACE_F_PANEL_CANVAS};
    rc=aster_surface_create(&desc,sizeof(desc),&surface);
    if (rc < 0) return rc;
    aster_ge_init(&ge,surface,width,height,commands,128,arena,sizeof(arena));
    aster_imu_pose_init(&pose);reference[0]=1;reference[1]=reference[2]=reference[3]=0;
    calibrate_start();status=0;touch_target=0;last_log=0;
    rc=aster_app_set_framerate(30);
    if (rc < 0) { release();return rc; }
    aster_printf("imu cube init %ux%u cache %u ms",width,height,imu.min_interval_ms);
    return ASTER_OK;
}

ASTER_APP_EXPORT void aster_app_event(const aster_event_t *ev)
{
    if (!ev || !surface) return;
    if (ev->type == ASTER_EVENT_FRAME) {
        aster_imu_sample_t next={.size=sizeof(next),.version=1};
        status=aster_imu_read(&next,sizeof(next));
        if (!status) {
            int32_t changed=aster_imu_pose_update(&pose,&next);
            if (changed < 0) status=changed;
            else { sample=next;if (changed && calibrating) calibration(&next); }
        }
        uint32_t now=aster_time_now_ms();
        if ((uint32_t)(now-last_log) >= 2000) {
            last_log=now;
            aster_printf("imu sample seq=%u err=%d a=%d,%d,%d g=%d,%d,%d",
                sample.sequence,status,(int)(sample.accel[0]*1000),(int)(sample.accel[1]*1000),
                (int)(sample.accel[2]*1000),(int)(sample.gyro[0]*1000),
                (int)(sample.gyro[1]*1000),(int)(sample.gyro[2]*1000));
        }
        aster_gfx_fonts_pending();
        int32_t rc=render();
        if (rc < 0) { aster_errfmt("imu cube draw %d",rc);aster_app_exit(rc);return; }
    } else if (ev->type == ASTER_EVENT_TOUCH && ev->u.touch.slot == 0) {
        if (ev->u.touch.action == ASTER_TOUCH_DOWN)
            touch_target=hit_button(ev->u.touch.x,ev->u.touch.y);
        else if (ev->u.touch.action == ASTER_TOUCH_UP) {
            int hit=hit_button(ev->u.touch.x,ev->u.touch.y);
            if (hit && hit == touch_target) {
                if (hit == 1) { memcpy(reference,pose.q,sizeof(reference));aster_printf("imu zero"); }
                else { calibrate_start();aster_printf("imu calibration requested"); }
            }
            touch_target=0;
        }
    } else if (ev->type == ASTER_EVENT_KEY && ev->u.key.pressed && ev->u.key.code == 1) {
        aster_app_exit(0);return;
    }
}
ASTER_APP_EXPORT void aster_app_resume(void) { pose.last_ms=0;touch_target=0; }
ASTER_APP_EXPORT void aster_app_stop(void) { release(); }
ASTER_APP_EXPORT void aster_app_destroy(void) { release(); }
