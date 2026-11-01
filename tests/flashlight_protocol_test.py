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
    /* Light Config carries Flicker after the colour, and Cone is in pi/256:
       128 is a 90 degree beam and 255 is about 179.3, which is not clamped. */
    uint8_t all[]={2,255,40,255,0,128,255,0};recv_bytes(all,sizeof(all));
    struct flashlight_beam beam=flashlight_ext_beam(7);
    assert(beam.reach==40 && beam.cone==255 && beam.red==0 && beam.green==128 && beam.flicker==0);
    /* A server built before Flicker existed sends one byte fewer: steady. */
    uint8_t legacy[]={2,255,40,90,0,128,255};recv_bytes(legacy,sizeof(legacy));
    beam=flashlight_ext_beam(7);
    assert(beam.reach==40 && beam.cone==90 && beam.flicker==0);
    /* Flicker is kept, and a light with it drops out now and then. */
    all[7]=255; recv_bytes(all,sizeof(all));
    assert(flashlight_ext_beam(7).flicker==255);
    /* Dark about Flicker/512 of the time: half here. */
    int dark=0; for(int i=0;i<2000;i++) if(flashlight_ext_dark(7,(double)i/12.0)) dark++;
    assert(dark > 800 && dark < 1200);
    all[7]=0; recv_bytes(all,sizeof(all));
    for(int i=0;i<2000;i++) assert(!flashlight_ext_dark(7,(double)i/12.0));
    uint8_t personal[]={2,7,0,0,255,0,0,0};recv_bytes(personal,sizeof(personal));
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
    puts("Flashlight V1 packet, config, flicker, bitmask, lifecycle and request tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'flashlight_test.c'; exe=Path(d)/'flashlight_test'
    c.write_text(harness)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(c),
                    str(root/'src/flashlight_ext.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
