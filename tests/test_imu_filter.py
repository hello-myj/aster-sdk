"""Check physical scenarios against the APP-side quaternion filter."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class ImuFilter(unittest.TestCase):
    def test_stationary_rotation_duplicates_gaps_and_gravity(self):
        code=r'''
#include <assert.h>
#include <math.h>
#include <aster_imu_filter.h>
static int near(float a,float b,float eps) { return fabsf(a-b)<eps; }
int main(void) {
    aster_imu_pose_t p;aster_imu_pose_init(&p);
    aster_imu_sample_t s={.size=sizeof(s),.version=1,.flags=3,.sequence=1,.accel={0,0,9.80665f}};
    assert(aster_imu_pose_update(&p,&s)==1);
    for (int i=0;i<300;i++) { s.sequence++;s.timestamp_ms+=10;assert(aster_imu_pose_update(&p,&s)==1); }
    assert(near(p.q[0],1,.001f));
    s.gyro[2]=1.5707963f;
    for (int i=0;i<100;i++) { s.sequence++;s.timestamp_ms+=10;aster_imu_pose_update(&p,&s); }
    float v[3]={1,0,0},out[3];aster_imu_quat_rotate(p.q,v,out);
    assert(near(out[0],0,.002f) && near(out[1],1,.002f)); /* 90 deg yaw in 1 s */
    float saved[4];memcpy(saved,p.q,sizeof(saved));
    assert(aster_imu_pose_update(&p,&s)==0 && !memcmp(saved,p.q,sizeof(saved)));
    s.sequence++;s.timestamp_ms+=1000;
    assert(aster_imu_pose_update(&p,&s)==1 && !memcmp(saved,p.q,sizeof(saved)));
    float relative[4];aster_imu_pose_relative(&p,saved,relative);
    assert(near(relative[0],1,.001f) && near(relative[1],0,.001f));
    s.gyro[0]=NAN;s.sequence++;
    assert(aster_imu_pose_update(&p,&s)==ASTER_EINVAL);
    assert(!memcmp(saved,p.q,sizeof(saved)));
    aster_imu_pose_init(&p);s.gyro[0]=s.gyro[2]=0;s.accel[0]=9.80665f;s.accel[2]=0;
    assert(aster_imu_pose_update(&p,&s)==1);
    float gravity[3]={1,0,0};aster_imu_quat_rotate(p.q,gravity,out);
    assert(near(out[0],0,.002f) && near(out[2],1,.002f));
    aster_imu_pose_init(&p);s.accel[0]=0;s.accel[2]=-9.80665f;
    aster_imu_pose_update(&p,&s);gravity[0]=0;gravity[2]=-1;
    aster_imu_quat_rotate(p.q,gravity,out);assert(near(out[2],1,.002f));
    aster_imu_pose_init(&p);s.accel[2]=9.80665f;s.gyro[2]=.03f;p.bias[2]=.03f;
    for(int i=0;i<300;i++) { s.sequence++;s.timestamp_ms+=10;aster_imu_pose_update(&p,&s); }
    assert(near(p.q[3],0,.001f)); /* stationary gyro bias removed */
    s.gyro[2]=0;memset(p.bias,0,sizeof(p.bias));
    p.q[0]=.9238795f;p.q[1]=.3826834f;p.q[2]=p.q[3]=0;
    for(int i=0;i<500;i++) { s.sequence++;s.timestamp_ms+=10;aster_imu_pose_update(&p,&s); }
    assert(near(p.q[1],0,.001f)); /* gravity corrects an erroneous roll */
    memcpy(saved,p.q,sizeof(saved));s.accel[0]=30;
    for(int i=0;i<100;i++) { s.sequence++;s.timestamp_ms+=10;aster_imu_pose_update(&p,&s); }
    assert(near(p.q[1],saved[1],.0001f) && near(p.q[2],saved[2],.0001f));
    /* Strong linear acceleration must not become a false gravity tilt. */
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            executable=str(Path(directory)/'filter')
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-x','c','-',
                '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/aster'),'-lm','-o',executable],
                input=code,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            subprocess.run([executable],check=True,timeout=5)

    def test_cube_roll_pitch_mapping_preserves_yaw(self):
        source=(ROOT/'apps/imucube/src/main.c').read_text()
        mapping=source[source.index('static void cube_visual_quat'):source.index('static void render_cube')]
        harness=r'''
#include <assert.h>
#include <math.h>
#include <aster_imu_filter.h>
static aster_imu_pose_t pose;
static float reference[4]={1,0,0,0};
static void euler(float r,float p,float y,float q[4]) {
    float cr=cosf(r/2),sr=sinf(r/2),cp=cosf(p/2),sp=sinf(p/2),cy=cosf(y/2),sy=sinf(y/2);
    q[0]=cr*cp*cy+sr*sp*sy;q[1]=sr*cp*cy-cr*sp*sy;
    q[2]=cr*sp*cy+sr*cp*sy;q[3]=cr*cp*sy-sr*sp*cy;
}
'''
        cases=r'''
int main(void) {
    const float angles[4][3]={{.6f,0,0},{0,.6f,0},{0,0,.6f},{.6f,-.4f,.8f}};
    for(int ref=0;ref<2;ref++) {
        euler(.2f*ref,-.3f*ref,.4f*ref,reference);
        for(int i=0;i<4;i++) {
            float r[4],expected[4],actual[4],saved[4];
            euler(angles[i][0],angles[i][1],angles[i][2],r);
            float w=reference[0],x=reference[1],y=reference[2],z=reference[3];
            pose.q[0]=w*r[0]-x*r[1]-y*r[2]-z*r[3];
            pose.q[1]=w*r[1]+x*r[0]+y*r[3]-z*r[2];
            pose.q[2]=w*r[2]-x*r[3]+y*r[0]+z*r[1];
            pose.q[3]=w*r[3]+x*r[2]-y*r[1]+z*r[0];
            memcpy(saved,pose.q,sizeof(saved));cube_visual_quat(actual);
            euler(-angles[i][0],-angles[i][1],angles[i][2],expected);
            for(int j=0;j<4;j++) assert(fabsf(actual[j]-expected[j])<.00002f);
            assert(!memcmp(saved,pose.q,sizeof(saved)));
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            executable=str(Path(directory)/'mapping')
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-x','c','-',
                '-I'+str(ROOT/'include'),'-I'+str(ROOT/'lib/aster'),'-lm','-o',executable],
                input=harness+mapping+cases,text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            subprocess.run([executable],check=True,timeout=5)
