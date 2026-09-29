/* Pie menu content/interaction port from ZeroSpades' PieMenuView.cpp,
 * Copyright (c) 2026 Francois ND; GPL-3.0-or-later. */
#ifndef PIE_MENU_H
#define PIE_MENU_H
#include <stdbool.h>
bool pie_menu_open(void);
bool pie_menu_opened(void);
bool pie_menu_has_selection(void);
void pie_menu_move(float dx, float dy);
void pie_menu_cycle(int direction);
void pie_menu_update(float dt);
void pie_menu_draw(void);
void pie_menu_close(bool commit);
/* Standalone unlabelled ping at the current crosshair; no chat fallback. */
#endif
