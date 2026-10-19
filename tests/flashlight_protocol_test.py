"""Compile real Flashlight V1 state machine; run: python3 tests/flashlight_protocol_test.py."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "flashlight_ext.h"
static void recv_bytes(const uint8_t *p, size_t n) {flashlight_ext_receive(p, n);}
int main(void) {
    uint8_t light[3]={0,7,1}, request[3]={0};
    assert(FLASHLIGHT_EXT_ID==0x32 && FLASHLIGHT_PACKET_ID==0x72);
    recv_bytes(light,3); assert(!flashlight_ext_on(7));
    flashlight_ext_set_negotiated(true);
    recv_bytes(light,2); assert(!flashlight_ext_on(7));
    recv_bytes(light,3); assert(flashlight_ext_on(7));
    flashlight_ext_request(7,request); assert(request[0]==0 && request[1]==7 && request[2]==0);
    light[2]=2;recv_bytes(light,3);assert(flashlight_ext_on(7));
    uint8_t state[]={1,0x81,0x02};recv_bytes(state,sizeof(state));
    assert(flashlight_ext_on(0) && flashlight_ext_on(7) && flashlight_ext_on(9));
    assert(!flashlight_ext_on(8) && !flashlight_ext_on(255));
    uint8_t all[]={2,255,40,255,0,128,255};recv_bytes(all,sizeof(all));
    struct flashlight_beam beam=flashlight_ext_beam(7);
    assert(beam.reach==40 && beam.cone==179 && beam.red==0 && beam.green==128);
    uint8_t personal[]={2,7,0,0,255,0,0};recv_bytes(personal,sizeof(personal));
    beam=flashlight_ext_beam(7);assert(beam.reach==0 && beam.cone==0);
    all[2]=55; recv_bytes(all,sizeof(all));assert(flashlight_ext_beam(7).reach==0);
    assert(flashlight_ext_beam(8).reach==55);
    flashlight_ext_reset_player(7,false);assert(!flashlight_ext_on(7) && flashlight_ext_beam(7).reach==0);
    flashlight_ext_reset_player(7,true);assert(flashlight_ext_beam(7).reach==55);
    flashlight_ext_reset_map();assert(!flashlight_ext_on(0) && flashlight_ext_beam(8).reach==55);
    flashlight_ext_reset_connection();assert(!flashlight_ext_negotiated());
    assert(flashlight_ext_beam(8).reach==60);
    flashlight_ext_set_negotiated(true);
    assert(!flashlight_ext_on(7) && flashlight_ext_beam(7).reach==60);
    puts("Flashlight V1 packet, config, bitmask, lifecycle and request tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'flashlight_test.c'; exe=Path(d)/'flashlight_test'
    c.write_text(harness)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(c),
                    str(root/'src/flashlight_ext.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
