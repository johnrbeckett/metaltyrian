/* Dumps the HUD pictures (tyrian.pic 3 and 6) and the sidekick HUD icons
 * (OPTION_SHAPES) to PPM files, for laying out MetalTyrian's crisp HUD text.
 * Usage: dump_hud DATA_DIR OUT_DIR */
#include "file.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "SDL.h"
#include <stdio.h>
#include <string.h>

static void save(SDL_Surface *s, const char *path)
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
}

int main(int argc, char **argv)
{
	custom_data_dir = argv[1];
	char path[512];
	JE_loadPals();
	SDL_Surface *s = SDL_CreateRGBSurface(0, 320, 200, 8, 0, 0, 0, 0);
	for (int n = 1; n <= 13; n++)
	{
		JE_loadPic(s, n, false);
		snprintf(path, sizeof path, "%s/pic%02d.ppm", argv[2], n);
		save(s, path);
	}
	JE_loadPic(s, 3, false);  // game palette
	JE_loadMainShapeTables("tyrian.shp");
	unsigned count = sprite_table[OPTION_SHAPES].count;
	for (unsigned i = 0; i < count; i++)
	{
		if (!sprite_exists(OPTION_SHAPES, i))
			continue;
		const Sprite *sp = sprite(OPTION_SHAPES, i);
		SDL_Surface *t = SDL_CreateRGBSurface(0, sp->width, sp->height, 8, 0, 0, 0, 0);
		memset(t->pixels, 0, (size_t)t->pitch * t->h);
		blit_sprite(t, 0, 0, OPTION_SHAPES, i);
		snprintf(path, sizeof path, "%s/opt_%02u.ppm", argv[2], i);
		save(t, path);
		SDL_FreeSurface(t);
	}
	printf("option shapes: %u\n", count);
	return 0;
}
