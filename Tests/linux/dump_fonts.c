/* Dumps Tyrian's three sprite fonts (large, normal, tiny) as shading-level
 * maps, plus the game palettes, to JSON for MetalTyrian's crisp-font work.
 * Usage: dump_fonts DATA_DIR OUT.json */
#include "file.h"
#include "fonthand.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "SDL.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	custom_data_dir = argv[1];
	JE_loadPals();
	JE_loadMainShapeTables("tyrian.shp");
	SDL_Surface *s = SDL_CreateRGBSurface(0, 64, 64, 8, 0, 0, 0, 0);
	FILE *f = fopen(argv[2], "w");
	const struct { const char *name; unsigned table; } fonts[] = {
		{ "large", FONT_SHAPES }, { "normal", SMALL_FONT_SHAPES }, { "tiny", TINY_FONT } };
	fprintf(f, "{\"fonts\":{");
	for (int fi = 0; fi < 3; fi++)
	{
		fprintf(f, "%s\"%s\":{", fi ? "," : "", fonts[fi].name);
		int first = 1;
		for (int c = 33; c < 127; c++)
		{
			int id = font_ascii[c];
			if (id < 0 || (unsigned)id >= sprite_table[fonts[fi].table].count || !sprite_exists(fonts[fi].table, id))
				continue;
			const Sprite *sp = sprite(fonts[fi].table, id);
			memset(s->pixels, 255, (size_t)s->pitch * s->h);
			blit_sprite_hv(s, 0, 0, fonts[fi].table, id, 0, 0);
			fprintf(f, "%s\"%d\":{\"w\":%d,\"h\":%d,\"px\":[", first ? "" : ",", c, sp->width, sp->height);
			first = 0;
			for (int y = 0; y < sp->height; y++)
				for (int x = 0; x < sp->width; x++)
				{
					int v = ((Uint8 *)s->pixels)[y * s->pitch + x];
					fprintf(f, "%s%d", (x || y) ? "," : "", v == 255 ? -1 : v);
				}
			fprintf(f, "]}");
		}
		fprintf(f, "}");
	}
	fprintf(f, "},\"palettes\":[");
	for (int p = 0; p < palette_count; p++)
	{
		fprintf(f, "%s[", p ? "," : "");
		for (int i = 0; i < 256; i++)
			fprintf(f, "%s%d,%d,%d", i ? "," : "", palettes[p][i].r, palettes[p][i].g, palettes[p][i].b);
		fprintf(f, "]");
	}
	fprintf(f, "]}\n");
	fclose(f);
	return 0;
}
