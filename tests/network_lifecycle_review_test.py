from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'src/network.c').read_text()
def get(a,b): i=s.index(a);return s[i:s.index(b,i)]
h=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define log_error(...) ((void)0)
#define ENET_EVENT_TYPE_RECEIVE 1
#define ENET_EVENT_TYPE_DISCONNECT 2
typedef struct {int n;} ENetHost; typedef struct {int n;} ENetPeer; typedef struct {int type;void* packet;} ENetEvent;
ENetHost h; ENetPeer p; ENetHost* client;ENetPeer* peer; int created,destroyed,compressed,resets,disconnects,fail_create,event_type,playing;
ENetHost* enet_host_create(void* a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;created++;return fail_create?NULL:&h;}
void enet_host_destroy(ENetHost* x){assert(x==&h);destroyed++;} void enet_host_compress_with_range_coder(ENetHost* x){assert(x==&h);compressed++;}
void enet_peer_reset(ENetPeer* x){assert(x==&p);resets++;} void enet_peer_disconnect(ENetPeer* x,int a){(void)a;assert(x==&p);disconnects++;}
int enet_host_service(ENetHost* x,ENetEvent* e,int t){(void)x;(void)t;if(!event_type)return 0;e->type=event_type;event_type=0;return 1;}
void enet_packet_destroy(void* x){(void)x;}
int network_connected,network_logged_in,network_map_transfer_end;
#define NOOP(name) void name(void){}
NOOP(player_cancel_pending_block) NOOP(teamplay_reset_connection) NOOP(daytime_weather_reset_connection) NOOP(flashlight_ext_reset_connection)
NOOP(pie_menu_ext_reset_connection) NOOP(player_outfit_ext_reset_map) NOOP(client_key_reset_connection) NOOP(lighting_flashlight_reset)
NOOP(glowing_blocks_clear) NOOP(demo_playback_close) NOOP(player_stats_reset) NOOP(player_clear_corpses) NOOP(bloodmarks_clear) NOOP(damagenumbers_clear)
void damagenumbers_set_server_authoritative(int x){(void)x;} void player_outfit_ext_set_negotiated(int x){(void)x;} void chat_clear(int x){(void)x;}
int demo_is_playing(void){return playing;}
''' + get('void network_init_host(', '\nstatic inline char team_color_char') + get('void network_disconnect()', '\nint network_connect_sub') + r'''
int main(void){
 network_init_host();assert(created==1 && compressed==1 && client==&h);
 peer=&p;network_init_host();assert(destroyed==1 && !peer && client==&h);
 peer=&p;network_connected=1;event_type=ENET_EVENT_TYPE_DISCONNECT;network_disconnect();assert(destroyed==2 && !client && !peer && disconnects==1);
 network_init_host();peer=&p;network_connected=1;network_disconnect();assert(destroyed==3 && resets==1 && !client && !peer);
 network_init_host();peer=&p;network_disconnect();assert(destroyed==4 && resets==2 && !client && !peer);
 fail_create=1;network_init_host();assert(!client && !peer);network_disconnect();assert(destroyed==4);
 puts("network lifecycle behavior passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
