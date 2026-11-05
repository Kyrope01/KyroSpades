#include <assert.h>
#include "../src/legacy_mod_skin.h"
int main(void) {
    assert(legacy_mod_reload_suffix("Desert Eagle Blaze"));
    assert(legacy_mod_reload_suffix("USP-S BluePrint"));
    assert(!legacy_mod_reload_suffix("Kar-98K"));
    assert(!legacy_mod_reload_suffix("Default"));
    return 0;
}
