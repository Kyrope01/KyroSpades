"""Check the CTF intel flags decode the way the AoS 0.75 wire format defines.

aosprotocol 0.75, GameState:
    Intel flags | UByte | bits signal if teams have intel
                          - bit 1 for team 1, bit 2 for team 2

So wire bit 0 means "team 1 is *holding* an intel", i.e. team 2's intel is the
one that is gone. This client's fields mean the opposite -- team_N_intel is set
when team N's own intel is being carried -- and the flags used to be two
bitfields whose bit order is implementation-defined, which put them the wrong
way round. Symptom: joining a game already in progress drew the intel model on
the wrong player.

Run: python3 tests/intel_flags_test.py
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]

harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "network.h"

static int failures = 0;
static void check(const char* what, int ok) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if(!ok) failures++;
}

/* The wire GameState gamemode_data, exactly as the server sends it. */
struct wire_ctf {
    unsigned char team_1_score;
    unsigned char team_2_score;
    unsigned char capture_limit;
    unsigned char intels;
    unsigned char team_1_intel_location[12];
    unsigned char team_2_intel_location[12];
    float team_1_base[3];
    float team_2_base[3];
};

int main(void) {
    printf("wire layout (the client memcpy's this straight into struct GM_CTF)\n");
    printf("  sizeof(struct GM_CTF) = %zu (wire %zu)\n", sizeof(struct GM_CTF),
           sizeof(struct wire_ctf));
    printf("  offsetof(intels)      = %zu\n", offsetof(struct GM_CTF, intels));
    check("struct GM_CTF matches the 52-byte wire size",
          sizeof(struct GM_CTF) == 52 && sizeof(struct GM_CTF) == sizeof(struct wire_ctf));
    check("intel flags land on wire byte 3", offsetof(struct GM_CTF, intels) == 3);
    check("team 1 intel location lands on wire byte 4",
          offsetof(struct GM_CTF, team_1_intel_location) == 4);
    check("team 2 intel location lands on wire byte 16",
          offsetof(struct GM_CTF, team_2_intel_location) == 16);

    /* The reported bug: join a game where a team 1 player is already carrying
       team 2's intel. Wire bit 0 is set and team 2's intel location names the
       carrier. */
    printf("\nmid-game join, someone is already carrying an intel\n");
    /* Deliberately written with literal wire bits rather than the
       TEAMn_HAS_INTEL constants, so this is an independent check against the
       protocol spec instead of a restatement of the implementation. */
    for(int bit = 0; bit < 2; bit++) {
        struct wire_ctf w;
        memset(&w, 0, sizeof(w));
        w.intels = (unsigned char)(1u << bit);
        /* spec: bit 0 = team 1 is carrying, so it is team 2's intel that is
           gone and team 2's location that names the carrier. */
        unsigned char* slot = (bit == 0) ? w.team_2_intel_location
                                         : w.team_1_intel_location;
        slot[0] = 7; /* holding player ID */

        struct GM_CTF ctf;
        memset(&ctf, 0, sizeof(ctf));
        memcpy(&ctf, &w, sizeof(struct GM_CTF)); /* what read_PacketGameState does */

        const char* who = (bit == 0) ? "team 1 carries -> team 2's intel gone"
                                     : "team 2 carries -> team 1's intel gone";
        printf("  wire bit %d: %s\n", bit, who);
        printf("    TEAM_1_INTEL_HELD=%d  TEAM_2_INTEL_HELD=%d\n",
               TEAM_1_INTEL_HELD(ctf), TEAM_2_INTEL_HELD(ctf));

        /* The client draws the intel on whoever the *missing* intel's location
           names, gated on that same intel being flagged as held. Both halves
           must agree, or the model goes on the wrong player. */
        if(bit == 0) {
            check("team 2's intel is flagged held", TEAM_2_INTEL_HELD(ctf) == 1);
            check("team 1's intel is not", TEAM_1_INTEL_HELD(ctf) == 0);
            check("carrier id 7 pairs with team 2's intel location",
                  TEAM_2_INTEL_HELD(ctf)
                          && ctf.team_2_intel_location.held.player_id == 7);
            check("the pairing does NOT point at team 1's location",
                  !(TEAM_1_INTEL_HELD(ctf)
                    && ctf.team_1_intel_location.held.player_id == 7));
        } else {
            check("team 1's intel is flagged held", TEAM_1_INTEL_HELD(ctf) == 1);
            check("team 2's intel is not", TEAM_2_INTEL_HELD(ctf) == 0);
            check("carrier id 7 pairs with team 1's intel location",
                  TEAM_1_INTEL_HELD(ctf)
                          && ctf.team_1_intel_location.held.player_id == 7);
            check("the pairing does NOT point at team 2's location",
                  !(TEAM_2_INTEL_HELD(ctf)
                    && ctf.team_2_intel_location.held.player_id == 7));
        }
    }

    /* Both intels out at once. */
    printf("\nboth intels carried\n");
    {
        struct wire_ctf w;
        memset(&w, 0, sizeof(w));
        w.intels = 0x03;
        struct GM_CTF ctf;
        memcpy(&ctf, &w, sizeof(struct GM_CTF));
        check("both flagged", TEAM_1_INTEL_HELD(ctf) == 1 && TEAM_2_INTEL_HELD(ctf) == 1);
    }

    /* The setters used by the pickup/drop handlers must round-trip through the
       same bits the wire uses, or the state drifts after the first event. */
    printf("\nsetter round-trip against the wire bits\n");
    {
        struct GM_CTF ctf;
        memset(&ctf, 0, sizeof(ctf));
        SET_TEAM_2_INTEL_HELD(ctf, 1); /* a team 1 player picked team 2's intel up */
        printf("  SET_TEAM_2_INTEL_HELD(1) -> intels = 0x%02x\n", ctf.intels);
        check("sets wire bit 0 (team 1 has intel)", ctf.intels == 0x01);
        check("reads back through the accessor", TEAM_2_INTEL_HELD(ctf) == 1);
        check("leaves the other team alone", TEAM_1_INTEL_HELD(ctf) == 0);

        SET_TEAM_1_INTEL_HELD(ctf, 1);
        printf("  SET_TEAM_1_INTEL_HELD(1) -> intels = 0x%02x\n", ctf.intels);
        check("sets wire bit 1 too", ctf.intels == 0x03);

        SET_TEAM_2_INTEL_HELD(ctf, 0); /* team 1 player dropped or captured */
        printf("  SET_TEAM_2_INTEL_HELD(0) -> intels = 0x%02x\n", ctf.intels);
        check("clears only wire bit 0", ctf.intels == 0x02);
        check("team 1's intel still flagged", TEAM_1_INTEL_HELD(ctf) == 1);
    }

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
'''

FLAGS = [
    '-I', str(root / 'src'), '-I', str(root / 'deps'),
    '-I', str(root / 'deps' / 'libvxl'),
    '-DGIT_COMMIT_HASH="t"',
]

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'intel_flags.c'
    exe = Path(d) / 'intel_flags'
    c.write_text(harness)
    subprocess.run(['cc', '-std=gnu99', '-Wall', *FLAGS, str(c), '-o', str(exe)],
                   check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0 or 'FAIL' in result.stdout \
            or 'all checks passed' not in result.stdout:
        raise SystemExit('intel flags tests failed')

print('CTF intel flags tests passed')
