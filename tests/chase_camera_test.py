"""Exercise the real chase boom, wall recovery and aim anchoring without GL."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
controller = (root / 'src/cameracontroller.c').read_text()
chase = controller[controller.index('enum player_view_mode {'):controller.index('void cameracontroller_fps_render()')]
harness = r'''
#include <assert.h>
#include <math.h>
enum { CAMERAMODE_FPS, CAMERAMODE_SPECTATOR, CAMERA_HITTYPE_NONE, CAMERA_HITTYPE_BLOCK };
static int camera_mode, local_player_id, matrix_view;
static float camera_x=10, camera_y=5, camera_z=20, camera_rot_x, camera_rot_y=1.5707963f;
static float camera_crosshair_rot_x, camera_crosshair_rot_y;
static struct { int free_aim; } settings;
static struct Player { struct { struct { float x,y,z; } eye; } physics; } players[1] = {{{{10,4,20}}}};
static float player_height(struct Player *p) { (void)p; return 1; }
static float time_now, obstacle=-1, eye[3], target[3];
static float window_time(void) { return time_now; }
struct Camera_HitType { int type; float distance; };
static void camera_vector_from_angles(float rx,float ry,float *x,float *y,float *z) {
    *x=sinf(rx)*sinf(ry); *y=cosf(ry); *z=cosf(rx)*sinf(ry);
}
static void camera_hit_terrain(struct Camera_HitType *hit,float x,float y,float z,
                               float dx,float dy,float dz,float length) {
    (void)x;(void)y;(void)z;
    assert(fabsf(dx*dx+dy*dy+dz*dz-1)<0.0001f);
    hit->type=obstacle>=0&&obstacle<=length ? CAMERA_HITTYPE_BLOCK : CAMERA_HITTYPE_NONE;
    hit->distance=obstacle;
}
static void camera_hit(struct Camera_HitType *hit,int exclude,float x,float y,float z,
                       float dx,float dy,float dz,float range) {
    assert(exclude==0&&x==10&&y==5&&z==20&&fabsf(dx)<0.0001f&&fabsf(dy)<0.0001f&&dz>0.99f);
    assert(range==128);hit->type=CAMERA_HITTYPE_BLOCK;hit->distance=12;
}
static void matrix_lookAt(int view,float x,float y,float z,float a,float b,float c,float i,float j,float k) {
    (void)view;(void)i;(void)j;(void)k;
    eye[0]=x;eye[1]=y;eye[2]=z;target[0]=a;target[1]=b;target[2]=c;
}
''' + chase + r'''
int main(void) {
    assert(player_view==PLAYER_VIEW_FIRST_PERSON);
    cameracontroller_cycle_player_view();
    assert(player_view==PLAYER_VIEW_CHASE);
    assert(cameracontroller_chase_render());
    assert(eye[0]>10.8f&&eye[1]>5.6f&&eye[2]<16);
    assert(target[0]==10&&fabsf(target[1]-5)<0.0001f&&target[2]==32);
    obstacle=2;
    assert(cameracontroller_chase_render());
    assert(eye[2]>18&&eye[2]<19);
    float pulled_in = eye[2];
    obstacle=-1;time_now=0.016f;
    assert(cameracontroller_chase_render());
    assert(eye[2]<pulled_in&&eye[2]>16); /* Ease out rather than jumping. */
    obstacle=0.25f;
    assert(!cameracontroller_chase_render());
    assert(camera_x==10&&camera_y==5&&camera_z==20);
    chase_rendering=1;camera_mode=CAMERAMODE_SPECTATOR;
    assert(!cameracontroller_third_person_rendering());
    cameracontroller_cycle_player_view();
    assert(player_view==PLAYER_VIEW_FIRST_PERSON&&!chase_rendering);
}
'''
with TemporaryDirectory() as tmp:
    path = Path(tmp) / 'chase.c'
    exe = Path(tmp) / 'chase'
    path.write_text(harness)
    run(['cc', '-std=c99', str(path), '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)
print('Chase camera framing, collision, recovery and aim anchoring passed')
