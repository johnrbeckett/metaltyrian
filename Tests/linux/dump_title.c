/* Dumps the title screen art: background (tyrian.pic 4), the TYRIAN logo and
 * 2000(tm) sprites with transparency masks, and the composed title without
 * menu text. Usage: dump_title DATA_DIR OUT_DIR */
#include "file.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "SDL.h"
#include <stdio.h>
#include <string.h>

static void save(SDL_Surface *s, const char *path, const SDL_Surface *alt)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (int y = 0; y < s->h; y++)
		for (int x = 0; x < s->w; x++)
		{
			SDL_Color c = colors[((Uint8 *)s->pixels)[y * s->pitch + x]];
			fputc(c.r, f); fputc(c.g, f); fputc(c.b, f);
		}
	fclose(f);
	if (!alt)
		return;
	/* Alpha: opaque where the sprite drew (both blits agree). */
	char apath[600];
	snprintf(apath, sizeof apath, "%s.alpha.pgm", path);
	f = fopen(apath, "wb");
	fprintf(f, "P5\n%d %d\n255\n", s->w, s->h);
	for (int y = 0; y < s->h; y++)
		for (int x = 0; x < s->w; x++)
		{
			Uint8 a = ((Uint8 *)s->pixels)[y * s->pitch + x], b = ((Uint8 *)alt->pixels)[y * alt->pitch + x];
			fputc(a == b ? 255 : 0, f);
		}
	fclose(f);
}

static void sprite_out(unsigned id, const char *path)
{
	const Sprite *sp = sprite(PLANET_SHAPES, id);
	SDL_Surface *a = SDL_CreateRGBSurface(0, sp->width, sp->height, 8, 0, 0, 0, 0);
	SDL_Surface *b = SDL_CreateRGBSurface(0, sp->width, sp->height, 8, 0, 0, 0, 0);
	memset(a->pixels, 1, (size_t)a->pitch * a->h);
	memset(b->pixels, 2, (size_t)b->pitch * b->h);
	blit_sprite(a, 0, 0, PLANET_SHAPES, id);
	blit_sprite(b, 0, 0, PLANET_SHAPES, id);
	save(a, path, b);
}

int main(int argc, char **argv)
{
	custom_data_dir = argv[1];
	char path[512];
	JE_loadPals();
	JE_loadMainShapeTables("tyrian.shp");
	SDL_Surface *s = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	JE_loadPic(s, 4, false);
	snprintf(path, sizeof path, "%s/background.ppm", argv[2]); save(s, path, NULL);
	snprintf(path, sizeof path, "%s/logo_tyrian.ppm", argv[2]); sprite_out(146, path);
	snprintf(path, sizeof path, "%s/logo_2000.ppm", argv[2]); sprite_out(151, path);
	blit_sprite(s, 11, 4, PLANET_SHAPES, 146);
	blit_sprite(s, 155, 73, PLANET_SHAPES, 151);
	snprintf(path, sizeof path, "%s/title.ppm", argv[2]); save(s, path, NULL);
	return 0;
}
