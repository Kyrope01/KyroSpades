#ifndef SKY_H
#define SKY_H

#include <stdbool.h>

/* Draw with the current scene projection/view; false leaves the old sky intact. */
bool sky_render(void);
void sky_deinit(void);

#endif
