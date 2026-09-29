/* Teamplay v1 HUD rendering adapted from ZeroSpades; GPL-3.0-or-later. */
#ifndef TEAMPLAY_DRAW_H
#define TEAMPLAY_DRAW_H
void teamplay_draw_capture(void);
void teamplay_draw_world(void);
/* x,y are the top-left of a north-up map in GL bottom-left coordinates. */
void teamplay_draw_map(float left, float top, float width, float height,
                       float origin_x, float origin_y, float extent);
#endif
