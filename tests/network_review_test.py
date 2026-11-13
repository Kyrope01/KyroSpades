"""Run untrusted send/handshake/chat/address paths with fake ENet and UBSan."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/network.c').read_text()
def block(a,b):
 i=s.index(a); return s[i:s.index(b,i)]
send=block('#define NETWORK_SEND_MAX_BODY 511','\nstatic void network_note_glowing_build')
hand=block('void read_PacketHandshakeInit(', '\n/* Prototype')
chat=block('void read_PacketChatMessage(', '\n/* Damage Markers')
addr=block('int network_identifier_split(', '\nint network_connect_string(')
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "charset.h"
#define log_warn(...) ((void)0)
#define log_error(...) ((void)0)
#define PLAYERS_MAX 256
#define PLAYER_ID_SERVER 255
#define NETWORK_CUSTOM_REASON_MAX 252
#define PACKET_HANDSHAKERETURN_ID 32
#define TEAM_1 0
#define TEAM_2 1
#define TEAM_SPECTATOR 255
#define CHAT_ALL 0
#define CHAT_TEAM 1
#define CHAT_SYSTEM 2
#define CHAT_BIG 3
#define CHAT_INFO 4
#define CHAT_WARNING 5
#define CHAT_ERROR 6
#define SOUND_LOCAL 0
#define rgb(r,g,b) ((unsigned)((r)<<16|(g)<<8|(b)))
typedef uint32_t enet_uint32;
typedef struct { int x; } ENetPacket;
int network_connected=1; void* peer=(void*)1; struct {int outgoing;} network_stats[1];
int allocated, sent, destroyed, fail_alloc, fail_send; size_t packet_size;
ENetPacket fake;
ENetPacket* enet_packet_create(const void* b,size_t n,enet_uint32 f){(void)b;(void)f;allocated++;packet_size=n;return fail_alloc?NULL:&fake;}
int enet_peer_send(void* p,int ch,ENetPacket* x){(void)p;(void)ch;(void)x;sent++;return fail_send?-1:0;}
void enet_packet_destroy(ENetPacket* p){(void)p;destroyed++;}
int demo_is_playing(void){return 0;}
struct PacketChatMessage {unsigned char player_id,chat_type; char message[255];};
struct {int connected,team;char name[32];} players[256];
struct {struct {int red,green,blue;} team_1,team_2;} gamestate;
unsigned char local_player_id; char network_custom_reason[253];
int sound_beep1,sound_beep2; char shown[512]; int chats;
void sound_create(int t,void* s,float x,float y,float z){(void)t;(void)s;(void)x;(void)y;(void)z;}
void chat_showpopup(const char* msg,float sec,unsigned c){(void)sec;(void)c;snprintf(shown,sizeof(shown),"%s",msg);}
void chat_add(int ch,unsigned c,const char* msg){(void)ch;(void)c;chats++;snprintf(shown,sizeof(shown),"%s",msg);}
char team_color_char(int t){(void)t;return 1;}
''' + send + r'''
void network_send(int id,void* data,int len){network_send_flags(id,data,len,0);}
''' + hand + chat + addr + r'''
int main(void){
 unsigned char bytes[2000]; memset(bytes,'x',sizeof(bytes));
 read_PacketHandshakeInit(bytes,512); assert(!allocated);
 read_PacketHandshakeInit(bytes,-1); read_PacketHandshakeInit(NULL,1); assert(!allocated);
 read_PacketHandshakeInit(bytes,511); assert(sent==1 && packet_size==512 && network_stats[0].outgoing==512);
 fail_alloc=1; network_send_flags(1,bytes,1,0); assert(sent==1);
 fail_alloc=0; fail_send=1; network_send_flags(1,bytes,1,0); assert(destroyed==1 && network_stats[0].outgoing==512);
 unsigned char shortchat[]={255,CHAT_BIG}; read_PacketChatMessage(shortchat,2); assert(!strcmp(shown,""));
 unsigned char popup[]={255,CHAT_INFO,'a','b'}; read_PacketChatMessage(popup,4); assert(!strcmp(shown,"ab"));
 bytes[0]=255;bytes[1]=CHAT_SYSTEM; read_PacketChatMessage(bytes,2000); assert(strlen(network_custom_reason)==252);
 bytes[0]=0;bytes[1]=CHAT_ALL; read_PacketChatMessage(bytes,2000); assert(chats==1 && strlen(shown)<=255);
 bytes[1]=200; read_PacketChatMessage(bytes,2000); assert(chats==1);
 char ip[32];int port;
 assert(network_identifier_split("aos://4294967295:32887",ip,&port) && !strcmp(ip,"255.255.255.255"));
 assert(!network_identifier_split("aos://4294967296",ip,&port));
 assert(!network_identifier_split("aos://-1",ip,&port)); assert(!network_identifier_split("aos://12abc",ip,&port));
 puts("network hostile input tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined,float-cast-overflow','-fno-sanitize-recover=all','-I',str(root/'src'),str(p/'test.c'),str(root/'src/charset.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
assert 'territory_count >\n           sizeof(p->gamemode_data.tc.territory)' in s
assert 'len < (int)sizeof(struct PacketMapStart076)' in s
assert 'if(!network_connected || !network_update())' in s
