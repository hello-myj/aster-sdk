/* SPDX-License-Identifier: MIT
 * APP-side six-axis complementary attitude filter; no libc math imports.
 * q is [w,x,y,z], a unit quaternion rotating sensor vectors into the world.
 * Accelerometer corrects tilt, not yaw. There is no magnetometer heading.
 */
#ifndef ASTER_IMU_FILTER_H
#define ASTER_IMU_FILTER_H
#include <aster/imu.h>
#include <aster/error.h>
#include "aster_libc.h"

typedef struct {
    float q[4];
    float bias[3];             /* stationary gyro bias, rad/s */
    uint64_t last_ms;
    uint32_t last_sequence;
    uint32_t started;
} aster_imu_pose_t;

/* Internal bounded IEEE-754 binary32 inverse square root. */
static inline float aster_imu_inv_sqrt(float x)
{
    if (!(x > 0.0f)) return 0.0f;
    union { float f; uint32_t u; } v = { .f = x };
    v.u = 0x5f3759dfu - (v.u >> 1);
    float y = v.f;
    y *= 1.5f - 0.5f*x*y*y;
    y *= 1.5f - 0.5f*x*y*y;
    return y;
}

static inline void aster_imu_quat_normalize(float q[4])
{
    float k = aster_imu_inv_sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if (!k) { q[0]=1; q[1]=q[2]=q[3]=0; return; }
    for (int i=0;i<4;i++) q[i] *= k;
}

static inline void aster_imu_pose_init(aster_imu_pose_t *p)
{
    memset(p, 0, sizeof(*p));
    p->q[0] = 1;
}

/* consumed=1, duplicate=0, malformed sample=EINVAL. First sample establishes
 * gravity tilt. Gaps over 250 ms rebase the time, never integrate stale gyro.
 * Hardware axes are preserved; APP sets its visual mapping separately. */
static inline int32_t aster_imu_pose_update(aster_imu_pose_t *p,
                                          const aster_imu_sample_t *s)
{
    if (!p || !s || s->size < sizeof(*s) || s->version != 1 ||
        (s->flags & 3u) != 3u) return ASTER_EINVAL;
    for (int i=0;i<3;i++) {
        if (!(s->accel[i] >= -1000000 && s->accel[i] <= 1000000 &&
              s->gyro[i] >= -10000 && s->gyro[i] <= 10000)) return ASTER_EINVAL;
    }
    if (p->started && s->sequence == p->last_sequence) return 0;
    uint64_t elapsed = p->started ? s->timestamp_ms - p->last_ms : 0;
    float a[3] = {s->accel[0],s->accel[1],s->accel[2]};
    float a2 = a[0]*a[0]+a[1]*a[1]+a[2]*a[2];
    float k = aster_imu_inv_sqrt(a2);
    for (int i=0;i<3;i++) a[i] *= k;
    if (!p->started && k) {
        if (a[2] < -0.999f) {
            p->q[0]=0; p->q[1]=1; p->q[2]=p->q[3]=0;
        } else {
            p->q[0]=1+a[2]; p->q[1]=a[1]; p->q[2]=-a[0]; p->q[3]=0;
            aster_imu_quat_normalize(p->q);
        }
    }
    p->started=1; p->last_ms=s->timestamp_ms; p->last_sequence=s->sequence;
    if (!elapsed || elapsed > 250) return 1;
    float w=p->q[0], x=p->q[1], y=p->q[2], z=p->q[3];
    float g[3] = {s->gyro[0]-p->bias[0],s->gyro[1]-p->bias[1],s->gyro[2]-p->bias[2]};
    /* Reject strong linear acceleration rather than treating it as tilt. */
    if (a2 > 64 && a2 < 144) {
        float v[3] = {2*(x*z-w*y),2*(w*x+y*z),w*w-x*x-y*y+z*z};
        g[0] += 2.0f*(a[1]*v[2]-a[2]*v[1]);
        g[1] += 2.0f*(a[2]*v[0]-a[0]*v[2]);
        g[2] += 2.0f*(a[0]*v[1]-a[1]*v[0]);
    }
    float h = (float)elapsed * 0.0005f;
    p->q[0] += (-x*g[0]-y*g[1]-z*g[2])*h;
    p->q[1] += ( w*g[0]+y*g[2]-z*g[1])*h;
    p->q[2] += ( w*g[1]+z*g[0]-x*g[2])*h;
    p->q[3] += ( w*g[2]+x*g[1]-y*g[0])*h;
    aster_imu_quat_normalize(p->q);
    return 1;
}

/* Rotate a sensor-space vector into world space. out may alias v. */
static inline void aster_imu_quat_rotate(const float q[4], const float v[3], float out[3])
{
    float tx=2*(q[2]*v[2]-q[3]*v[1]);
    float ty=2*(q[3]*v[0]-q[1]*v[2]);
    float tz=2*(q[1]*v[1]-q[2]*v[0]);
    out[0]=v[0]+q[0]*tx+q[2]*tz-q[3]*ty;
    out[1]=v[1]+q[0]*ty+q[3]*tx-q[1]*tz;
    out[2]=v[2]+q[0]*tz+q[1]*ty-q[2]*tx;
}

/* Relative orientation = conjugate(reference) * current. */
static inline void aster_imu_pose_relative(const aster_imu_pose_t *p,
                                          const float reference[4], float out[4])
{
    float w=reference[0],x=-reference[1],y=-reference[2],z=-reference[3];
    const float *q=p->q;
    out[0]=w*q[0]-x*q[1]-y*q[2]-z*q[3];
    out[1]=w*q[1]+x*q[0]+y*q[3]-z*q[2];
    out[2]=w*q[2]-x*q[3]+y*q[0]+z*q[1];
    out[3]=w*q[3]+x*q[2]-y*q[1]+z*q[0];
    aster_imu_quat_normalize(out);
}
#endif
