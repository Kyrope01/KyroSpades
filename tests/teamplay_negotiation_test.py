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
#pragma pack(push,1)
struct PacketExtInfoEntry {uint8_t id,version;};
struct PacketExtInfo {uint8_t length;struct PacketExtInfoEntry entries[256];};
#pragma pack(pop)
static bool negotiated, flashlight_negotiated, outfit_negotiated;
static int sends, sent_id, sent_len;
static struct PacketExtInfo reply;
static void teamplay_set_negotiated(bool yes) {negotiated=yes;}
static void flashlight_ext_set_negotiated(bool yes) {flashlight_negotiated=yes;}
static void player_outfit_ext_set_negotiated(bool yes) {outfit_negotiated=yes;}
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
    puts("Teamplay, Flashlight, and Player Outfit extension v1 handshake tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'handshake.c'
    exe=Path(d)/'handshake'
    c.write_text(stub)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
