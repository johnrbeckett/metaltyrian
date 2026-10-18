/*
 * MetalTyrian: crisp text. The game's font routines note every letter they
 * draw; HUD labels painted into art are registered as labels. When a frame is
 * shown, each noted letter or label is checked against the frame pixel for
 * pixel, and the ones still there are handed to the app, which draws them
 * crisply over the frame. Not part of OpenTyrian.
 */
#ifndef MT_TEXT_H
#define MT_TEXT_H

#ifdef METALTYRIAN

#include "SDL.h"
#include <stdbool.h>

/* From the sprite blitters, for the three font tables. */
void mt_text_glyph(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value, bool unsafe);
void mt_text_shadow(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index, bool black);

/* From blit_sprite_hv_blend: see-through text, which stays as pixels (only
 * noted, for the leak finder). */
void mt_text_blend_glyph(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index);

/* A label painted into art: the box (game pixels, exclusive x1/y1) holds the
 * text; its current pixels become the check. vertical: one letter, centred. */
void mt_text_label(SDL_Surface *surface, int x0, int y0, int x1, int y1, const char *text, bool vertical);
/* A sidekick HUD icon at x, y: registers its word (the green lettering in its
 * top rows) as a label. */
void mt_text_icon_word(SDL_Surface *surface, int x, int y, unsigned int icon);
/* Forget all labels (a new HUD panel is being drawn). */
void mt_text_clear_labels(void);

/* From mouse.c: the pointer was drawn into VGAScreen over the 24x28 box at
 * x, y, whose pixels before were `under`; or it was taken away again. */
void mt_text_cursor_drawn(int x, int y, const Uint8 *under);
void mt_text_cursor_removed(void);

/* Called with each frame as it is shown: mt_text_present before
 * mt_present_frame, mt_text_presented after. */
void mt_text_present(SDL_Surface *frame);
void mt_text_presented(SDL_Surface *frame);

#endif
#endif
