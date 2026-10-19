/*
 * MetalTyrian: Setup > Controls > Keys. Each action has two keys, both always
 * live: the right-handed default and the left-handed one. Pick a key, press
 * Return (or click it), then press the new key. A key already used by another
 * action swaps with it, so nothing is left doubled or unreachable.
 *
 * GNU GPL version 2 or later, as OpenTyrian.
 */
#ifdef METALTYRIAN

#include "mt_keys.h"

#include "fonthand.h"
#include "font.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "nortsong.h"
#include "sndmast.h"
#include "vga256d.h"
#include "video.h"

#include "metaltyrian_host.h"

#include <stdio.h>
#include <string.h>

static const char *const action_names[MT_ACT_COUNT] = {
	"Up", "Down", "Left", "Right", "Fire", "Sidekicks", "Rear Mode", "Superbomb",
	"Autofire Main", "Autofire Sidekicks", "Autofire Specials",
};

enum { ROW_RESET = MT_ACT_COUNT, ROW_DONE, ROW_COUNT };

static const int xName = 40, xSlot[2] = { 170, 232 }, wSlot = 56;
static const int yRows = 36, dyRow = 11, hRow = 10;  /* 11 actions + Reset and Done above the help line */

/* Keys that stay the game's own. */
static bool reserved(SDL_Scancode sc)
{
	return sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_P || sc == SDL_SCANCODE_LGUI ||
	       sc == SDL_SCANCODE_RGUI || sc == SDL_SCANCODE_UNKNOWN;
}

/* Put `sc` on (action, slot); another action using it takes the old key. */
static void assign(int action, int slot, int sc)
{
	const int old = mt_key_binding(action, slot);
	for (int a = 0; a < MT_ACT_COUNT; ++a)
		for (int s = 0; s < 2; ++s)
			if (a != action && mt_key_binding(a, s) == sc)
				mt_set_key_binding(a, s, old);
	mt_set_key_binding(action, slot, sc);
}

static int row_at(int y)
{
	for (int r = 0; r < ROW_COUNT; ++r)
	{
		const int ry = yRows + dyRow * r + (r >= ROW_RESET ? 6 : 0);
		if (y >= ry && y < ry + hRow)
			return r;
	}
	return -1;
}

void mt_keys_menu(void)
{
	int row = 0, slot = 0;
	bool capture = false;

	for (; ; )
	{
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
		draw_font_hv_shadow(VGAScreen, 160, 4, "Keys", large_font, centered, 15, -3, false, 2);
		draw_font_hv_shadow(VGAScreen, xSlot[0] + wSlot / 2, 24, "Right hand", small_font, centered, 15, 2, false, 1);
		draw_font_hv_shadow(VGAScreen, xSlot[1] + wSlot / 2, 24, "Left hand", small_font, centered, 15, 2, false, 1);

		for (int r = 0; r < ROW_COUNT; ++r)
		{
			const int y = yRows + dyRow * r + (r >= ROW_RESET ? 6 : 0);
			const bool sel = r == row;
			const char *name = r == ROW_RESET ? "Reset to defaults" : r == ROW_DONE ? "Done" : action_names[r];
			draw_font_hv_shadow(VGAScreen, xName, y, name, normal_font, left_aligned, 15, -3 + (sel ? 2 : 0), false, 2);
			if (r >= MT_ACT_COUNT)
				continue;
			for (int s = 0; s < 2; ++s)
			{
				const bool here = sel && s == slot;
				const char *key = here && capture ? "Press a key" : mt_key_name(mt_key_binding(r, s));
				draw_font_hv_shadow(VGAScreen, xSlot[s] + wSlot / 2, y, key, here && capture ? small_font : normal_font,
				                    centered, 15, -3 + (here ? 3 : 0) + (mt_key_binding(r, s) != mt_key_default(r, s) ? 1 : 0), false, 2);
				if (here)
					JE_rectangle(VGAScreen, xSlot[s] - 2, y - 2, xSlot[s] + wSlot + 1, y + hRow, 248);
			}
		}

		const char *help = capture ? "Press the new key. Esc: keep the old one."
		                 : row == ROW_RESET ? "Put every key back as it was."
		                 : row == ROW_DONE ? "Return to the previous menu."
		                 : "Return or click to change. Left/right: other hand.";
		JE_textShade(VGAScreen, xName, 190, help, 15, 4, PART_SHADE);

		service_SDL_events(true);
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool moved = false;
		do
		{
			SDL_Delay(16);
			const int ox = mouse_x, oy = mouse_y;
			if (!capture)
				push_joysticks_as_keyboard();
			service_SDL_events(false);
			moved = mouse_x != ox || mouse_y != oy;
		} while (!(newkey || newmouse || moved));

		if (capture)
		{
			if (newkey)
			{
				if (lastkey_scan == SDL_SCANCODE_ESCAPE)
				{
					JE_playSampleNum(S_SPRING);
					capture = false;
				}
				else if (reserved(lastkey_scan))
				{
					JE_playSampleNum(S_CLINK);
				}
				else
				{
					JE_playSampleNum(S_SELECT);
					assign(row, slot, lastkey_scan);
					capture = false;
				}
			}
			else if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			{
				capture = false;
			}
			continue;
		}

		bool act = false;
		if (moved || newmouse)
		{
			const int r = row_at(mouse_y);
			if (r >= 0 && mouse_x >= xName - 4 && mouse_x < xSlot[1] + wSlot + 4)
			{
				int s = slot;
				if (r < MT_ACT_COUNT)
				{
					if (mouse_x >= xSlot[1] - 3)
						s = 1;
					else if (mouse_x >= xSlot[0] - 3)
						s = 0;
				}
				if (r != row || s != slot)
				{
					JE_playSampleNum(S_CURSOR);
					row = r;
					slot = s;
				}
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT && row_at(lastmouse_y) == r)
					act = true;
			}
			if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				return;
			}
		}
		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				row = (row + ROW_COUNT - 1) % ROW_COUNT;
				break;
			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				row = (row + 1) % ROW_COUNT;
				break;
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				if (row < MT_ACT_COUNT)
				{
					JE_playSampleNum(S_CURSOR);
					slot ^= 1;
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				act = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				return;
			default:
				break;
			}
		}
		if (act)
		{
			if (row == ROW_DONE)
			{
				JE_playSampleNum(S_SELECT);
				return;
			}
			if (row == ROW_RESET)
			{
				JE_playSampleNum(S_SELECT);
				for (int a = 0; a < MT_ACT_COUNT; ++a)
					for (int s = 0; s < 2; ++s)
						mt_set_key_binding(a, s, 0);
			}
			else
			{
				JE_playSampleNum(S_CLICK);
				capture = true;
				/* A key already held (the Return that chose this) is not the new key. */
				service_SDL_events(true);
			}
		}
	}
}

#endif /* METALTYRIAN */
