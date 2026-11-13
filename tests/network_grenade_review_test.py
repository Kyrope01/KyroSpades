from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/network.c').read_text(); a=s.index('void read_PacketGrenade('); b=s.index('void read_PacketSetHP(',a)
harness=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#define log_warn(...) ((void)0)
struct PacketGrenade { unsigned char player_id; float fuse_length,x,y,z,vx,vy,vz; };
struct {int team;} players[256];
struct V {float x,y,z;};
struct Grenade {int team,owner_id;float fuse_length;struct V pos,velocity;};
int count; struct Grenade last;
void grenade_add(struct Grenade* g){count++;last=*g;}
''' + s[a:b] + r'''
int main(void){
 struct PacketGrenade p={1,3,1,2,3,0.1,0.2,0.3};
 read_PacketGrenade(&p,sizeof p);assert(count==1 && last.pos.y==60);
 p.x=NAN;read_PacketGrenade(&p,sizeof p);assert(count==1); p.x=0;
 p.vz=INFINITY;read_PacketGrenade(&p,sizeof p);assert(count==1);p.vz=1e30f;read_PacketGrenade(&p,sizeof p);assert(count==1);
 p.vz=0;p.fuse_length=INFINITY;read_PacketGrenade(&p,sizeof p);assert(count==1);
 puts("network grenade validation passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined,float-cast-overflow','-fno-sanitize-recover=all',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
