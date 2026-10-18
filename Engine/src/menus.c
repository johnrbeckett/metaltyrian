/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "menus.h"

#include "config.h"
#include "episodes.h"
#include "font.h"
#include "fonthand.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"
#include "varz.h"
#include "loudness.h"

char episode_name[6][31];
char difficulty_name[7][21];
char gameplay_name[GAMEPLAY_NAME_COUNT][26];
char timed_battle_name[4][23];

bool gameplaySelect(void)
{
	enum MenuItemIndex
	{
		MENU_ITEM_1_PLAYER_FULL_GAME = 0,
		MENU_ITEM_1_PLAYER_ARCADE,
		MENU_ITEM_1_PLAYER_TIMED_BATTLE,
		MENU_ITEM_2_PLAYER_ARCADE,
		MENU_ITEM_NETWORK,
	};

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

#ifdef METALTYRIAN
	/* No network play in MetalTyrian: the Network row (the last one) is not shown. */
	const size_t menuItemsCount = MENU_ITEM_NETWORK;
#else
	const size_t menuItemsCount = COUNTOF(gameplay_name) - 1;
#endif
	size_t selectedIndex = MENU_ITEM_1_PLAYER_FULL_GAME;

	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int yMenuItems = 54;
	const int dyMenuItems = 24;
	const int hMenuItem = 13;
	int wMenuItem[COUNTOF(gameplay_name) - 1] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, gameplay_name[0], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const char *const text = gameplay_name[i + 1];

			wMenuItem[i] = JE_textWidth(text, normal_font);
			const int x = xCenter - wMenuItem[i] / 2;
			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == selectedIndex;
			const bool disabled = i == MENU_ITEM_NETWORK;

			draw_font_hv_shadow(VGAScreen, x, y, text, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(16);

			Uint16 oldMouseX = mouse_x;
			Uint16 oldMouseY = mouse_y;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_x != oldMouseX || mouse_y != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool cancel = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < menuItemsCount; ++i)
			{
				const int xMenuItem = xCenter - wMenuItem[i] / 2;
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
				{
					const int yMenuItem = yMenuItems + dyMenuItems * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			switch (selectedIndex)
			{
			case MENU_ITEM_1_PLAYER_FULL_GAME:
			case MENU_ITEM_1_PLAYER_ARCADE:
			case MENU_ITEM_1_PLAYER_TIMED_BATTLE:
			case MENU_ITEM_2_PLAYER_ARCADE:
			{
				JE_playSampleNum(S_SELECT);

				fade_black(10);

				onePlayerAction = selectedIndex == MENU_ITEM_1_PLAYER_ARCADE;
				timedBattleMode = selectedIndex == MENU_ITEM_1_PLAYER_TIMED_BATTLE;
				twoPlayerMode = selectedIndex == MENU_ITEM_2_PLAYER_ARCADE;
				return true;
			}
			case MENU_ITEM_NETWORK:
			{
				JE_playSampleNum(S_SPRING);
				break;
			}
			default:
				break;
			}
		}

		if (cancel)
		{
			fade_black(15);

			return false;
		}
	}
}

bool episodeSelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	const size_t menuItemsCount = EPISODE_AVAILABLE;
	size_t selectedIndex = 0;

	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int xMenuItem = 20;
	const int yMenuItems = 50;
	const int dyMenuItems = 30;
	const int hMenuItem = 13;
	int wMenuItem[EPISODE_AVAILABLE] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, episode_name[0], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const char *const text = episode_name[i + 1];

			wMenuItem[i] = JE_textWidth(text, normal_font);
			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == selectedIndex;
			const bool disabled = !episodeAvail[i];

			draw_font_hv_shadow(VGAScreen, xMenuItem, y, text, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0) + (disabled ? -4 : 0), false, 2);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(16);

			Uint16 oldMouseX = mouse_x;
			Uint16 oldMouseY = mouse_y;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			NETWORK_KEEP_ALIVE();

			mouseMoved = mouse_x != oldMouseX || mouse_y != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool cancel = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < menuItemsCount; ++i)
			{
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
				{
					const int yMenuItem = yMenuItems + dyMenuItems * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			if (episodeAvail[selectedIndex])
			{
				JE_playSampleNum(S_SELECT);

				fade_black(10);

				JE_initEpisode(selectedIndex + 1);
				initial_episode_num = episodeNum;
				return true;
			}
			else
			{
				JE_playSampleNum(S_SPRING);
			}
		}

		if (cancel)
		{
			fade_black(15);

			return false;
		}
	}
}

bool difficultySelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	const size_t menuItemsCount = COUNTOF(difficulty_name) - 1;
	size_t menuItemsVisibleCount = menuItemsCount - 3;
	size_t selectedIndex = 1;
	size_t lordProgress = 0;

	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int yMenuItems = 54;
	const int dyMenuItems = 24;
	const int hMenuItem = 13;
	int wMenuItem[COUNTOF(difficulty_name) - 1] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, difficulty_name[0], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsVisibleCount; ++i)
		{
			const char *const text = difficulty_name[i + 1];

			wMenuItem[i] = JE_textWidth(text, normal_font);
			const int x = xCenter - wMenuItem[i] / 2;
			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == selectedIndex;

			draw_font_hv_shadow(VGAScreen, x, y, text, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(16);

			Uint16 oldMouseX = mouse_x;
			Uint16 oldMouseY = mouse_y;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			NETWORK_KEEP_ALIVE();

			mouseMoved = mouse_x != oldMouseX || mouse_y != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		if (menuItemsVisibleCount == 5)
		{
			const SDL_Scancode lordKeys[] = { SDL_SCANCODE_L, SDL_SCANCODE_O, SDL_SCANCODE_R, SDL_SCANCODE_D };

			for (size_t i = 0; ; ++i)
			{
				if (i == COUNTOF(lordKeys))
				{
					menuItemsVisibleCount = 6;
					break;
				}

				if (!keysactive[lordKeys[i]])
					break;
			}

			if (newkey)
			{
				// Due to key jamming, holding down 4 keys simultaneous may not be possible, so an
				// alternate input method is also provided.
				if (lordProgress < COUNTOF(lordKeys) &&
					lastkey_scan == lordKeys[lordProgress])
				{
					lordProgress += 1;

					if (lordProgress == COUNTOF(lordKeys))
						menuItemsVisibleCount = 6;
				}
				else
				{
					lordProgress = 0;
				}
			}
		}
		if (SDL_GetModState() & KMOD_SHIFT)
		{
			if (menuItemsVisibleCount == 4 && keysactive[SDL_SCANCODE_RIGHTBRACKET])
				menuItemsVisibleCount = 5;
			if (menuItemsVisibleCount == 3 && keysactive[SDL_SCANCODE_G])
				menuItemsVisibleCount = 4;
		}

		bool action = false;
		bool cancel = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < menuItemsVisibleCount; ++i)
			{
				const int xMenuItem = xCenter - wMenuItem[i] / 2;
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
				{
					const int yMenuItem = yMenuItems + dyMenuItems * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsVisibleCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsVisibleCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			switch (selectedIndex)
			{
			case 0:
				difficultyLevel = DIFFICULTY_EASY;
				break;
			case 1:
				difficultyLevel = DIFFICULTY_NORMAL;
				break;
			case 2:
				difficultyLevel = DIFFICULTY_HARD;
				break;
			case 3:
				difficultyLevel = DIFFICULTY_IMPOSSIBLE;
				break;
			case 4:
				difficultyLevel = DIFFICULTY_SUICIDE;
				break;
			case 5:
				difficultyLevel = DIFFICULTY_ZINGLON;
				break;
			}

			fade_black(10);

			return true;
		}

		if (cancel)
		{
			fade_black(15);

			return false;
		}
	}
}

#ifdef METALTYRIAN
/* The ship as it flies (its in-game sprite, level, not banking), drawn
 * scale times larger with its centre at cx, cy. */
static void mt_draw_ship_big(SDL_Surface *surface, unsigned int ship, int cx, int cy)
{
	static SDL_Surface *a = NULL, *b = NULL;
	if (a == NULL)
	{
		/* As wide as the screen: the sprite blitters step rows by the screen's pitch. */
		a = SDL_CreateRGBSurface(0, 320, 32, 8, 0, 0, 0, 0);
		b = SDL_CreateRGBSurface(0, 320, 32, 8, 0, 0, 0, 0);
	}
	const unsigned int gfx = ships[ship].shipgraphic;
	Sprite2_array *sheet = gfx > 500 ? &spriteSheetT2000 : &spriteSheet9;
	const unsigned int index = gfx > 500 ? gfx - 500 : gfx;
	/* Drawn twice over two different fills: where both agree, the ship is there. */
	SDL_Surface *s2[2] = { a, b };
	for (int k = 0; k < 2; k++)
	{
		SDL_FillRect(s2[k], NULL, k ? 254 : 255);
		if (gfx == 1)
		{
			/* Nort Ship: two halves side by side. */
			blit_sprite2x2(s2[k], 8, 2, spriteSheet9, 220);
			blit_sprite2x2(s2[k], 32, 2, spriteSheet9, 222);
		}
		else
			blit_sprite2x2(s2[k], 20, 2, *sheet, index);
	}
	int x0 = 64, x1 = -1, y0 = 32, y1 = -1;
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 64; x++)
			if (((Uint8 *)a->pixels)[y * a->pitch + x] == ((Uint8 *)b->pixels)[y * b->pitch + x])
			{
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	if (x1 < x0)
		return;
	const int w = x1 - x0 + 1, h = y1 - y0 + 1;
	int scale = 4;
	while (scale > 1 && (w * scale > 110 || h * scale > 120))
		scale--;
	const int ox = cx - w * scale / 2, oy = cy - h * scale / 2;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const Uint8 v = ((Uint8 *)a->pixels)[(y0 + y) * a->pitch + x0 + x];
			if (v != ((Uint8 *)b->pixels)[(y0 + y) * b->pitch + x0 + x])
				continue;
			for (int sy = 0; sy < scale; sy++)
				for (int sx = 0; sx < scale; sx++)
				{
					const int px = ox + x * scale + sx, py = oy + y * scale + sy;
					if (px >= 0 && px < surface->w && py >= 0 && py < surface->h)
						((Uint8 *)surface->pixels)[py * surface->pitch + px] = v;
				}
		}
}

/* MetalTyrian: 1 Player Arcade lets you fly any ship in the game (the
 * original always gave you the Stalker). On the upgrade screen's picture: the
 * ship as it flies on the left with its armour, every ship listed on the
 * right. Arrows or hovering choose, Enter or a click flies. Ships differ in
 * armour; weapons still come from pickups. Returns the ship, or 0 for back. */
unsigned int mt_arcade_ship_select(unsigned int initial)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	unsigned int ship = (initial >= 1 && initial <= SHIP_NUM) ? initial : 8;
	bool restart = true;

	const int xList = 172, yList = 34, dyList = 8, wList = 136;

	for (; ; )
	{
		if (restart)
			JE_loadPic(VGAScreen2, 1, false);

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		mt_draw_ship_big(VGAScreen, ship, 78, 92);

		char line[40];
		snprintf(line, sizeof(line), "Armor %u", ships[ship].dmg);
		draw_font_hv_shadow(VGAScreen, 78, 172, line, small_font, centered, 15, 2, false, 1);

		draw_font_hv_shadow(VGAScreen, 237, 9, "Choose Ship", large_font, centered, 15, -3, false, 2);

		for (unsigned int i = 1; i <= SHIP_NUM; i++)
		{
			char name[32];
			snprintf(name, sizeof(name), "%s", ships[i].name);
			char *gap = strstr(name, "   ");   /* "USP Talon   Light Fighter": the name only */
			if (gap)
				*gap = '\0';
			const bool selected = i == ship;
			draw_font_hv_shadow(VGAScreen, xList, yList + dyList * (int)(i - 1), name, small_font, left_aligned,
			                    15, selected ? 4 : -1, false, 1);
		}

		draw_font_hv_shadow(VGAScreen, 10, 189, "Pick a ship, then Enter or click to fly.  Esc: back.", small_font, left_aligned, 15, 0, false, 1);

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;
			fade_palette(colors, 10, 0, 255);
			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(16);

			Uint16 oldMouseX = mouse_x;
			Uint16 oldMouseY = mouse_y;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_x != oldMouseX || mouse_y != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		bool action = false, cancel = false;
		unsigned int pick = ship;

		/* Hovering a name picks it, as in the other menus; clicking it flies. */
		if (mouseMoved || newmouse)
		{
			const int mx = newmouse ? lastmouse_x : mouse_x, my = newmouse ? lastmouse_y : mouse_y;
			if (mx >= xList - 4 && mx < xList + wList && my >= yList - 1 && my < yList - 1 + dyList * SHIP_NUM)
			{
				pick = (unsigned int)((my - (yList - 1)) / dyList) + 1;
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
					action = true;
			}
			else if (newmouse && lastmouse_but == SDL_BUTTON_LEFT && mx < 160 && my < 160)
				action = true;   /* clicking the ship flies it too */
		}
		if (newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
			cancel = true;
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_UP:
				pick = ship == 1 ? SHIP_NUM : ship - 1;
				break;
			case SDL_SCANCODE_RIGHT:
			case SDL_SCANCODE_DOWN:
				pick = ship == SHIP_NUM ? 1 : ship + 1;
				break;
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
				action = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				cancel = true;
				break;
			default:
				break;
			}
		}

		if (pick != ship)
		{
			JE_playSampleNum(S_CURSOR);
			ship = pick;
		}
		if (action)
		{
			JE_playSampleNum(S_SELECT);
			fade_black(10);
			return ship;
		}
		if (cancel)
		{
			JE_playSampleNum(S_SPRING);
			fade_black(15);
			return 0;
		}
	}
}
#endif

bool timedBattleSelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	const size_t menuItemsCount = COUNTOF(timed_battle_name) - 1;
	size_t selectedIndex = 0;

	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int yMenuItems = 54;
	const int dyMenuItems = 24;
	const int hMenuItem = 13;
	int wMenuItem[COUNTOF(timed_battle_name) - 1] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, timed_battle_name[0], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const char *const text = timed_battle_name[i + 1];

			wMenuItem[i] = JE_textWidth(text, normal_font);
			const int x = xCenter - wMenuItem[i] / 2;
			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == selectedIndex;

			draw_font_hv_shadow(VGAScreen, x, y, text, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(16);

			Uint16 oldMouseX = mouse_x;
			Uint16 oldMouseY = mouse_y;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			NETWORK_KEEP_ALIVE();

			mouseMoved = mouse_x != oldMouseX || mouse_y != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool cancel = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < menuItemsCount; ++i)
			{
				const int xMenuItem = xCenter - wMenuItem[i] / 2;
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
				{
					const int yMenuItem = yMenuItems + dyMenuItems * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				cancel = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			fade_black(10);

			// I don't know how T2K determines episode from this,
			// I'm assuming it's hardcoded
			JE_initEpisode(selectedIndex == 0 ? 1 : 5);
			timeBattleSelection = selectedIndex + 1;
			return true;
		}

		if (cancel)
		{
			fade_black(15);

			return false;
		}
	}
}
