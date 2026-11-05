#ifndef LEGACY_MOD_SKIN_H
#define LEGACY_MOD_SKIN_H

#include <stdbool.h>
#include <string.h>

/* Only these imported packs supply reload sounds. Preserve old behavior for
   all pre-existing skins, which previously used the default reload sound. */
static inline bool legacy_mod_reload_suffix(const char* name) {
    static const char* const names[] = {"USP-S BluePrint", "USP-S RoyalBlue",
        "Desert Eagle Asiimov", "Desert Eagle Blaze", "M9 Diamond"};
    for(unsigned i=0; i<sizeof(names)/sizeof(names[0]); ++i)
        if(strcmp(name,names[i]) == 0) return true;
    return false;
}
#endif
