/*
 * MetalTyrian: generous mouse targets for OpenTyrian's list menus.
 *
 * OpenTyrian counts a click only inside the original word (its pixel-font
 * width, 13 pixels tall), leaving dead strips between rows and beside short
 * words; with crisp text the visible word and that box don't quite agree.
 * Here a row covers its whole pitch (the gap split between neighbours) and at
 * least 120 pixels across, centred on the item.
 *
 * GNU GPL version 2 or later, as OpenTyrian.
 */
#ifndef MT_MENU_H
#define MT_MENU_H

#include <stdbool.h>

#ifdef METALTYRIAN
static inline bool mt_hit_x(int mx, int x, int w)
{
	const int centre = x + w / 2;
	int half = w / 2 + 24;
	if (half < 60)
		half = 60;
	return mx >= centre - half && mx < centre + half;
}

static inline bool mt_hit_y(int my, int y, int h, int dy)
{
	if (dy < h)
		dy = h;
	const int top = y - (dy - h) / 2;
	return my >= top && my < top + dy;
}
#define MT_HIT_X(mx, x, w) mt_hit_x((mx), (x), (w))
#define MT_HIT_Y(my, y, h, dy) mt_hit_y((my), (y), (h), (dy))
#else
#define MT_HIT_X(mx, x, w) ((mx) >= (x) && (mx) < (x) + (w))
#define MT_HIT_Y(my, y, h, dy) ((my) >= (y) && (my) < (y) + (h))
#endif

#endif
