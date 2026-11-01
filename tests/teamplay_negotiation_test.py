"""Compile real KyroSpades extension handshake and check ZeroSpades v1 wire reply.

Run: python3 tests/teamplay_negotiation_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/network.c').read_text()
handler = source[source.index('void read_PacketExtInfo(void* data, int len) {'):
                 source.index('void read_PacketPlayerProperties(', source.index('void read_PacketExtInfo(void* data, int len) {'))]
stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define log_info(...) ((void)0)
#define PACKET_EXTINFO_ID 60
#define TEAMPLAY_EXTENSION_ID 48
#define FLASHLIGHT_EXT_ID 0x32
#define EXT_PLAYER_PROPERTIES 0
#define EXT_256PLAYERS 192
#define EXT_MESSAGES 193
#define EXT_KICKREASON 194
#define EXT_PLAYER_OUTFIT 0x35
#define EXT_DAMAGE_MARKERS 0x20
#define PIE_MENU_EXT_ID 0x31
#define EXT_CLIENT_KEY 0x02
#define EXT_DAYTIME_WEATHER 0x33
#define DAYTIME_WEATHER_VERSION 1
#pragma pack(push,1)
struct PacketExtInfoEntry {uint8_t id,version;};
struct PacketExtInfo {uint8_t length;struct PacketExtInfoEntry entries[256];};
#pragma pack(pop)
static bool negotiated, flashlight_negotiated, outfit_negotiated, pie_menu_negotiated;
static bool damage_markers_negotiated;
static bool client_key_negotiated, client_key_allowed = true;
static bool daytime_weather_negotiated;
static int sends, sent_id, sent_len;
static struct PacketExtInfo reply;
static void teamplay_set_negotiated(bool yes) {negotiated=yes;}
static void flashlight_ext_set_negotiated(bool yes) {flashlight_negotiated=yes;}
static void player_outfit_ext_set_negotiated(bool yes) {outfit_negotiated=yes;}
static void pie_menu_ext_set_negotiated(bool yes) {pie_menu_negotiated=yes;}
static void damagenumbers_set_server_authoritative(bool yes) {damage_markers_negotiated=yes;}
static void client_key_set_negotiated(bool yes) {client_key_negotiated=yes;}
static void daytime_weather_set_negotiated(bool yes) {daytime_weather_negotiated=yes;}
static bool client_key_enabled(void) {return client_key_allowed;}
static void network_send(int id,void* data,int len) {
    sent_id=id;sent_len=len;sends++;memcpy(&reply,data,(size_t)len);
}
''' + handler + r'''
int main(void) {
    struct PacketExtInfo pkt={0};
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){48,1};
    read_PacketExtInfo(&pkt,3);
    assert(negotiated && sends==1 && sent_id==60 && sent_len==11);
    assert(reply.length==5 && reply.entries[4].id==48 && reply.entries[4].version==1);
    assert(!damage_markers_negotiated);
    pkt.entries[0].version=2; /* ZeroSpades advertises its implemented v1. */
    read_PacketExtInfo(&pkt,3);
    assert(negotiated && sends==2 && reply.entries[4].version==1);
    pkt.entries[0].version=0;
    read_PacketExtInfo(&pkt,3);
    assert(!negotiated && sends==3 && reply.length==4 && sent_len==9);
    pkt.entries[0]=(struct PacketExtInfoEntry){47,1};
    read_PacketExtInfo(&pkt,3);
    assert(!negotiated && sends==4 && reply.length==4);
    pkt.entries[0]=(struct PacketExtInfoEntry){48,1};
    read_PacketExtInfo(&pkt,2); /* truncated; no reply and no negotiation */
    assert(sends==4 && !negotiated);
    pkt.entries[0]=(struct PacketExtInfoEntry){FLASHLIGHT_EXT_ID,2};
    read_PacketExtInfo(&pkt,3);
    assert(!negotiated && flashlight_negotiated && reply.length==5 && sent_len==11);
    assert(reply.entries[4].id==FLASHLIGHT_EXT_ID && reply.entries[4].version==1);
    pkt.length=2;pkt.entries[1]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    read_PacketExtInfo(&pkt,5);
    assert(negotiated && flashlight_negotiated && reply.length==6 && sent_len==13);
    assert(reply.entries[4].id==TEAMPLAY_EXTENSION_ID && reply.entries[5].id==FLASHLIGHT_EXT_ID);
    pkt.entries[0].version=0;
    read_PacketExtInfo(&pkt,5);
    assert(negotiated && !flashlight_negotiated && reply.length==5);
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){EXT_PLAYER_OUTFIT,1};
    read_PacketExtInfo(&pkt,3);
    assert(outfit_negotiated && reply.length==5 && sent_len==11);
    assert(reply.entries[4].id==EXT_PLAYER_OUTFIT && reply.entries[4].version==1);
    pkt.length=3;
    pkt.entries[0]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    pkt.entries[1]=(struct PacketExtInfoEntry){FLASHLIGHT_EXT_ID,1};
    pkt.entries[2]=(struct PacketExtInfoEntry){EXT_PLAYER_OUTFIT,1};
    read_PacketExtInfo(&pkt,7);
    assert(negotiated && flashlight_negotiated && outfit_negotiated);
    assert(reply.length==7 && sent_len==15);
    assert(reply.entries[4].id==TEAMPLAY_EXTENSION_ID);
    assert(reply.entries[5].id==FLASHLIGHT_EXT_ID);
    assert(reply.entries[6].id==EXT_PLAYER_OUTFIT);
    /* Pie Menu v1: offered, answered with the v1 this client implements. */
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){PIE_MENU_EXT_ID,1};
    read_PacketExtInfo(&pkt,3);
    assert(pie_menu_negotiated && reply.length==5 && sent_len==11);
    assert(reply.entries[4].id==PIE_MENU_EXT_ID && reply.entries[4].version==1);
    pkt.entries[0].version=3; /* a newer offer still gets the v1 reply */
    read_PacketExtInfo(&pkt,3);
    assert(pie_menu_negotiated && reply.entries[4].version==1);
    pkt.entries[0].version=0;
    read_PacketExtInfo(&pkt,3);
    assert(!pie_menu_negotiated && reply.length==4);
    pkt.entries[0]=(struct PacketExtInfoEntry){0x34,1}; /* a neighbour id is not it */
    read_PacketExtInfo(&pkt,3);
    assert(!pie_menu_negotiated && reply.length==4);
    pkt.length=4;
    pkt.entries[0]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    pkt.entries[1]=(struct PacketExtInfoEntry){FLASHLIGHT_EXT_ID,1};
    pkt.entries[2]=(struct PacketExtInfoEntry){EXT_PLAYER_OUTFIT,1};
    pkt.entries[3]=(struct PacketExtInfoEntry){PIE_MENU_EXT_ID,1};
    read_PacketExtInfo(&pkt,9);
    assert(negotiated && flashlight_negotiated && outfit_negotiated && pie_menu_negotiated);
    assert(reply.length==8 && sent_len==17);
    assert(reply.entries[4].id==TEAMPLAY_EXTENSION_ID);
    assert(reply.entries[5].id==FLASHLIGHT_EXT_ID);
    assert(reply.entries[6].id==EXT_PLAYER_OUTFIT);
    assert(reply.entries[7].id==PIE_MENU_EXT_ID && reply.entries[7].version==1);
    /* Damage Markers: offered by the server, answered, and the client then
       stops estimating the damage it deals itself. */
    assert(!damage_markers_negotiated);
    pkt.length=2;
    pkt.entries[0]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    pkt.entries[1]=(struct PacketExtInfoEntry){EXT_DAMAGE_MARKERS,1};
    read_PacketExtInfo(&pkt,5);
    assert(damage_markers_negotiated);
    assert(reply.length==6 && sent_len==13);
    assert(reply.entries[5].id==EXT_DAMAGE_MARKERS && reply.entries[5].version==1);
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    read_PacketExtInfo(&pkt,3);
    assert(!damage_markers_negotiated && reply.length==5);
    /* Client Key: answered only while the player allows it, so a server that
       logs the extension never sees a claim the client will not honour. */
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){EXT_CLIENT_KEY,1};
    read_PacketExtInfo(&pkt,3);
    assert(client_key_negotiated && reply.length==5 && sent_len==11);
    assert(reply.entries[4].id==EXT_CLIENT_KEY && reply.entries[4].version==1);
    client_key_allowed = false;
    read_PacketExtInfo(&pkt,3);
    assert(!client_key_negotiated && reply.length==4 && sent_len==9);
    /* Daytime and Weather: v1 is what this client implements, so v1 or newer
       is answered with v1 and v0 is not answered at all. */
    pkt.length=1;pkt.entries[0]=(struct PacketExtInfoEntry){EXT_DAYTIME_WEATHER,1};
    read_PacketExtInfo(&pkt,3);
    assert(daytime_weather_negotiated && reply.length==5 && sent_len==11);
    assert(reply.entries[4].id==EXT_DAYTIME_WEATHER && reply.entries[4].version==1);
    pkt.entries[0].version=2;
    read_PacketExtInfo(&pkt,3);
    assert(daytime_weather_negotiated && reply.entries[4].version==DAYTIME_WEATHER_VERSION);
    pkt.entries[0].version=0;
    read_PacketExtInfo(&pkt,3);
    assert(!daytime_weather_negotiated && reply.length==4);
    pkt.length=2;
    pkt.entries[0]=(struct PacketExtInfoEntry){TEAMPLAY_EXTENSION_ID,1};
    pkt.entries[1]=(struct PacketExtInfoEntry){EXT_DAYTIME_WEATHER,1};
    read_PacketExtInfo(&pkt,5);
    assert(negotiated && daytime_weather_negotiated && reply.length==6 && sent_len==13);
    assert(reply.entries[5].id==EXT_DAYTIME_WEATHER);
    puts("Teamplay, Flashlight, Player Outfit, Pie Menu, Damage Markers, Client Key and Daytime handshake tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'handshake.c'
    exe=Path(d)/'handshake'
    c.write_text(stub)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
