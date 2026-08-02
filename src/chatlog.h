
/*
	Copyright (c) 2017-2020 ByteBit

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CHATLOG_H
#define CHATLOG_H

#include "hud.h"

/*
	Chat Log HUD tab.

	A separate HUD reachable from the ESC menu when connected to a server.
	Displays the global chat history (channel 0) chronologically, preserves
	inline color codes, and lets the user select text with the mouse and copy
	it via Ctrl+C / right-click. The tab is hidden when not connected to a
	server.

	Find-in-chat: Ctrl+F (Cmd+F on macOS) opens a search bar at the top of
	the panel. All matches are highlighted, with the current one painted in
	a brighter color. Enter / F3 / Down advances to the next match, Shift
	with any of those steps backwards. Esc closes the bar.
*/
extern struct hud hud_chatlog;

/*
	Hooks the global text-input pipeline into the chatlog search bar.

	The chat log HUD doesn't use a microui textbox - selection, context
	menu, and the link modal are all hand-rolled here, and the search bar
	follows the same pattern. main.c::text_input() consults these so it
	can route raw UTF-8 characters into the search query buffer only when
	the bar is actually open and we're sitting on the chatlog HUD.

	chatlog_search_active() returns non-zero when the bar is open and
	accepting input; the dispatcher returns early after forwarding so the
	character isn't ALSO interpreted by other consumers downstream.
*/
int  chatlog_search_active(void);
void chatlog_search_text_input(const char* utf8);

#endif
