/*
 * MetalTyrian: full-screen pictures in high resolution. The game notes when it
 * loads the title picture and where it draws the two logos; when a frame
 * shows them, the app draws John's high-resolution title art in their place,
 * with everything else on the screen (menu, pointer, version) on top.
 * The other tyrian.pic pictures (menus, HUD, story screens) work the same
 * way with their regenerated art (App/Pictures): whatever the game draws over
 * a picture stays on top of the art. Not part of OpenTyrian.
 */
#ifndef MT_TITLE_H
#define MT_TITLE_H

#ifdef METALTYRIAN

#include "SDL.h"

#include <stdbool.h>

/* The title picture was just loaded into surface. */
void mt_title_picture(SDL_Surface *surface);
/* A logo (0 TYRIAN, 1 2000) was drawn at x, y. */
void mt_title_logo(int which, int x, int y);
/* Any tyrian.pic picture (1-14) was just loaded into surface (JE_loadPic). */
void mt_picture_loaded(SDL_Surface *surface, int number);
/* A sprite drawn as part of a picture at x, y counts as picture from now on
 * (the in-game message strip on the HUD). */
void mt_picture_sprite(int number, unsigned table, unsigned index, int x, int y);
/* Around the drawing of the playfield's own overlays (score, lives...) into
 * game_screen: what changes floats, and the app shades a soft shadow under it. */
void mt_float_begin(SDL_Surface *game);
/* Edge smoothing: start after the bottom background, mark after each layer,
 * rebase after a filter that reworks the whole playfield. */
void mt_edge_start(SDL_Surface *game);
void mt_edge_mark(SDL_Surface *game);
void mt_edge_rebase(SDL_Surface *game);
void mt_edge_cancel(void);   /* no smoothing for this frame */
/* The controls overlay on the next frame: opacity 0-255 and which hand's keys. */
void mt_controls_overlay(int alpha, bool lefty);
void mt_float_end(SDL_Surface *game);
/* With each shown frame, after mt_text_present, before mt_present_frame. */
void mt_title_present(SDL_Surface *frame);

#endif
#endif
