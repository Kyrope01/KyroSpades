"""Checks for the killfeed distance readout.

The number is the straight line between the killer's and the victim's eyes - the
path the bullet took - and it is appended to the kill line only when the client
has a position for both players. This compiles the real src/killfeed.c and
checks the maths, the formatting, the guards, and the wiring in network.c and
the settings.

Run: python3 tests/killfeed_distance_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

PROGRAM = r"""
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "killfeed.h"

static int failures;

static void expect_distance(const char* label, float got, float want) {
    if(fabsf(got - want) > 0.001F) {
        printf("FAIL %s: got %.4f want %.4f\n", label, (double)got, (double)want);
        failures++;
    } else {
        printf("ok %s\n", label);
    }
}

static void expect_line(const char* label, const char* got, const char* want) {
    if(strcmp(got, want) != 0) {
        printf("FAIL %s: got \"%s\" want \"%s\"\n", label, got, want);
        failures++;
    } else {
        printf("ok %s\n", label);
    }
}

int main(void) {
    /* 3-4-5, and the height difference counts: a shot from a tower is farther
       than the same shot at ground level. */
    expect_distance("flat", killfeed_distance(10.0F, 5.0F, 4.0F, 14.0F, 8.0F, 4.0F), 5.0F);
    expect_distance("vertical only", killfeed_distance(7.0F, 20.0F, -3.0F, 7.0F, 12.0F, -3.0F),
                    8.0F);
    expect_distance("same place", killfeed_distance(1.0F, 2.0F, 3.0F, 1.0F, 2.0F, 3.0F), 0.0F);

    /* An eye still at the origin means "no position yet": no number at all,
       never a map-wide one. */
    expect_distance("killer has no position",
                    killfeed_distance(0.0F, 0.0F, 0.0F, 30.0F, 10.0F, 4.0F), -1.0F);
    expect_distance("victim has no position",
                    killfeed_distance(30.0F, 10.0F, 4.0F, 0.0F, 0.0F, 0.0F), -1.0F);

    char line[256];
    strcpy(line, "Proman killed Zero (Rifle)");
    killfeed_append_distance(line, sizeof line, 42.46F);
    expect_line("suffix", line, "Proman killed Zero (Rifle) from 42.5 blocks");

    strcpy(line, "A killed B (Spade)");
    killfeed_append_distance(line, sizeof line, 0.0F);
    expect_line("melee range", line, "A killed B (Spade) from 0.0 blocks");

    strcpy(line, "A killed B (Rifle)");
    killfeed_append_distance(line, sizeof line, -1.0F);
    expect_line("no distance", line, "A killed B (Rifle)");

    /* A short buffer truncates instead of overflowing. */
    char small[24];
    memset(small, 'x', sizeof small);
    small[23] = 0;
    killfeed_append_distance(small, sizeof small, 1234.5F);
    expect_line("clipped", small, "xxxxxxxxxxxxxxxxxxxxxxx");

    char tight[16];
    strcpy(tight, "A killed B");
    killfeed_append_distance(tight, sizeof tight, 1234.5F);
    if(strlen(tight) >= sizeof tight) {
        printf("FAIL overflow: %s\n", tight);
        failures++;
    }
    printf("tight \"%s\"\n", tight);

    return failures ? 1 : 0;
}
"""

network = (root / "src/network.c").read_text()
config_header = (root / "src/config.h").read_text()
config_source = (root / "src/config.c").read_text()
config_ini = (root / "resources/config.ini").read_text()
main_source = (root / "src/main.c").read_text()

# Wiring: the readout is behind the setting, skips fake kills, and goes through
# the module rather than being inlined into the packet reader.
assert "if(settings.killfeed_distance && p->killer_id != p->player_id" in network
assert "p->kill_type != KILLTYPE_TEAMCHANGE && p->kill_type != KILLTYPE_CLASSCHANGE" in network
assert "killfeed_distance(killer->physics.eye.x" in network
assert "killfeed_append_distance(m, sizeof(m), distance);" in network
assert '#include "killfeed.h"' in network
assert network.count("killfeed_append_distance(") == 1

# The setting exists everywhere a setting has to.
assert "int killfeed_distance;" in config_header
assert 'config_seti("client", "killfeed_distance", settings.killfeed_distance);' in config_source
assert "IMPORT_SETTING(settings.killfeed_distance, killfeed_distance, atoi(value) ? 1 : 0);" in config_source
assert ".value = &settings_tmp.killfeed_distance," in config_source
assert "killfeed_distance              = 1" in config_ini
assert "settings.killfeed_distance = 1;" in main_source

# Build wiring.
assert "killfeed.c" in (root / "src/CMakeLists.txt").read_text()
assert "killfeed.c" in (root / "tools/bench/Makefile").read_text()

with tempfile.TemporaryDirectory() as tmp:
    workspace = Path(tmp)
    source = workspace / "killfeed_test.c"
    source.write_text(PROGRAM)
    binary = workspace / "killfeed_test"
    build = subprocess.run(
        ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
         "-I", str(root / "src"), str(source), str(root / "src/killfeed.c"),
         "-lm", "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    assert build.stderr == "", build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr

print("killfeed distance: ok")
