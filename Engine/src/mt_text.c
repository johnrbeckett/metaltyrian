/*
 * MetalTyrian: crisp text (see mt_text.h). Not part of OpenTyrian.
 */
#ifdef METALTYRIAN

#include "mt_text.h"

#include "fonthand.h"
#include "opentyr.h"
#include "palette.h"
#include "sprite.h"
#include "video.h"

#include "metaltyrian_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 200
#define MAX_RECS 4096
#define MAX_LABELS 48
#define MISS_LIMIT 300         /* shown frames a letter may go unseen before it is forgotten */

/* How a letter was drawn. A spot can show one of several (a menu item drawn
 * plain, then highlighted, then plain again from a saved copy). */
typedef struct
{
	Uint8 hue;
	Sint8 value;
	bool unsafe;
}
Variant;

#define VARIANTS 4

typedef struct
{
	bool used;
	Sint16 x, y;               /* screen position */
	Uint8 table, index;
	Variant v[VARIANTS];       /* most recent first */
	Uint8 nv;
	Uint32 outline_dark, outline_black;   /* see mt_text_item */
	bool outline_coloured;     /* the outline is the letter drawn darker, at outline_value */
	Sint8 outline_value;
	Uint16 misses;
	bool seen;                 /* passed the check in the frame being shown */
	bool ever_whole;           /* has been seen whole since it was drawn */
	Uint8 layer;               /* see grid */
	Sint16 twin;               /* the same letter's record on the other layer, + 1 */
	Sint8 whole_k;             /* this frame: variant seen whole, or -1 */
}
Rec;

typedef struct
{
	bool used, vertical;
	Sint16 x0, y0, x1, y1;
	char text[14];
	Uint8 hue, level;
	Uint8 pixels[64 * 24];     /* box pixels when registered */
	Uint8 is_text[64 * 24];    /* which of them are the lettering */
}
Label;

static Rec recs[MAX_RECS];
/* Letters drawn into game_screen are noted twice: layer 0 where the game
 * shows them (game_screen appears from column 24), layer 1 at the surface's
 * own coordinates, for when game_screen serves as a scratch picture copied
 * whole (the ship specs screen). Whichever matches the frame counts. */
static Sint16 grid[2][H][W];   /* rec index + 1 at each top-left position, 0 for none */
static int next_free = 0;
static Label labels[MAX_LABELS];

/* Recent font draws, to spot the darker copies drawn around a letter. */
#define RECENT 256
enum { DRAW_LETTER, DRAW_DARK, DRAW_BLACK };
static struct { Uint8 table, index, kind, layer; Sint8 value; Sint16 x, y; } recent[RECENT];
static int recent_pos = 0;

static void remember(int layer, unsigned int table, unsigned int index, int kind, Sint8 value, int x, int y)
{
	recent[recent_pos].layer = layer;
	recent[recent_pos].table = table;
	recent[recent_pos].index = index;
	recent[recent_pos].kind = kind;
	recent[recent_pos].value = value;
	recent[recent_pos].x = x;
	recent[recent_pos].y = y;
	recent_pos = (recent_pos + 1) % RECENT;
}

static int glyph_char[128];    /* font sprite index -> ASCII character, 0 if none */
static Uint8 body_level[3], cap_height[3];
static bool tables_ready = false;

static void prepare_tables(void)
{
	if (tables_ready)
		return;
	tables_ready = true;
	memset(glyph_char, 0, sizeof(glyph_char));
	for (int c = 33; c < 127; c++)
		if (font_ascii[c] >= 0 && font_ascii[c] < 128)
			glyph_char[font_ascii[c]] = c;
	for (int t = 0; t < 3; t++)
	{
		unsigned counts[16] = { 0 };
		for (unsigned i = 0; i < 86 && i < sprite_table[t].count; i++)
		{
			if (!sprite_exists(t, i))
				continue;
			const Sprite *sp = sprite(t, i);
			for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
			{
				if (*d == 255) { d++; continue; }
				if (*d == 254 || *d == 253) continue;
				counts[*d & 0x0f]++;
			}
		}
		unsigned best = 0;
		for (int l = 1; l < 16; l++)
			if (counts[l] > counts[best]) best = l;
		body_level[t] = best;
		int h_index = font_ascii['H'];
		cap_height[t] = sprite_exists(t, h_index) ? sprite(t, h_index)->height : 8;
	}
}

static bool is_font_table(unsigned int table)
{
	return table <= TINY_FONT;
}

static void to_screen(SDL_Surface *surface, int *x, int *y)
{
	(void)y;
	if (surface == game_screen)
		*x -= 24;  /* JE_starShowVGA shows game_screen from column 24 */
}

void mt_text_shadow(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index, bool black)
{
	if (!is_font_table(table) || index >= 128)
		return;
	if (surface == game_screen)
		remember(1, table, index, black ? DRAW_BLACK : DRAW_DARK, 0, x, y);
	to_screen(surface, &x, &y);
	remember(0, table, index, black ? DRAW_BLACK : DRAW_DARK, 0, x, y);
}

static void forget(int layer, int x, int y, unsigned int table, unsigned int index)
{
	if (x < 0 || y < 0 || x >= W || y >= H)
		return;
	int slot = grid[layer][y][x] - 1;
	if (slot >= 0 && recs[slot].table == table && recs[slot].index == index)
	{
		recs[slot].used = false;
		grid[layer][y][x] = 0;
	}
}

/* ---- Leak finder, for testing: MT_TEXT_LEAKS=DIR ---------------------------
 * Reports original-font pixels that would reach the screen with crisp text
 * on: letters that failed the check but are still mostly in the picture
 * (partly covered), see-through text, and characters without a crisp
 * version. Writes DIR/leaks.txt and a picture per new kind of leak, with
 * leaked pixels magenta and painted-out text grey. */
static const char *leak_dir = NULL;
static bool leak_env_read = false;
static struct { Sint16 x, y; Uint8 table, index; const char *why; } odd[256];
static int n_odd = 0;

static void note_odd(int x, int y, unsigned int table, unsigned int index, const char *why)
{
	if (!leak_env_read)
	{
		leak_env_read = true;
		leak_dir = getenv("MT_TEXT_LEAKS");
	}
	if (!leak_dir || n_odd >= 256)
		return;
	odd[n_odd].x = x; odd[n_odd].y = y;
	odd[n_odd].table = table; odd[n_odd].index = index;
	odd[n_odd].why = why;
	n_odd++;
}

void mt_text_blend_glyph(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index)
{
	if (!is_font_table(table) || index >= 128)
		return;
	to_screen(surface, &x, &y);
	note_odd(x, y, table, index, "see-through");
}

/* How many of a letter's pixels are in the frame as drawn; marks them. */
/* Which letter (record index + 1) last drew each pixel. */
static Sint16 owner[2][H][W];

/* How many of a letter's body pixels are in the frame as drawn; marks them.
 * With self >= 0, pixels another letter drew later don't count as matches. */
static int row_tot[64], row_hit[64];   /* per glyph row, filled by glyph_matches_own */
static int hit_x0, hit_y0, hit_x1, hit_y1;   /* box around the matches (exclusive x1, y1) */

static int glyph_matches_own(const Rec *r, const Variant *var, const Uint8 *frame, int pitch, int *total, Uint8 *mark, int self)
{
	memset(row_tot, 0, sizeof(row_tot));
	memset(row_hit, 0, sizeof(row_hit));
	hit_x0 = hit_y0 = 9999; hit_x1 = hit_y1 = -1;
	const Sprite *sp = sprite(r->table, r->index);
	const Uint8 hue = var->hue << 4;
	int px = r->x, py = r->y, xo = 0, hits = 0;
	*total = 0;
	for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
	{
		switch (*d)
		{
		case 255: d++; px += *d; xo += *d; break;
		case 254: px += sp->width - xo; xo = sp->width; break;
		case 253: px++; xo++; break;
		default:
		{
			Uint8 v;
			if (var->unsafe)
				v = hue | (Uint8)((*d & 0x0f) + var->value);
			else
			{
				Uint8 t = (*d & 0x0f) + var->value;
				if (t > 0xf)
					t = (t >= 0x1f) ? 0x0 : 0xf;
				v = hue | t;
			}
			/* Only the letter's solid body counts: its darkest edge pixels
			 * can match a plain background by chance. */
			if (px >= 0 && px < W && py >= 0 && py < H && (*d & 0x0f) + 1 >= body_level[r->table])
			{
				(*total)++;
				const int gr = py - r->y;
				if (gr >= 0 && gr < 64) row_tot[gr]++;
				if (frame[py * pitch + px] == v &&
				    (self < 0 || owner[r->layer][py][px] == 0 || owner[r->layer][py][px] == self + 1))
				{
					hits++;
					if (gr >= 0 && gr < 64) row_hit[gr]++;
					if (px < hit_x0) hit_x0 = px;
					if (px + 1 > hit_x1) hit_x1 = px + 1;
					if (py < hit_y0) hit_y0 = py;
					if (py + 1 > hit_y1) hit_y1 = py + 1;
					if (mark) mark[py * W + px] = 1;
				}
			}
			px++; xo++;
			break;
		}
		}
		if (xo >= sp->width) { xo = 0; px -= sp->width; py++; }
	}
	return hits;
}

static int glyph_matches(const Rec *r, const Variant *var, const Uint8 *frame, int pitch, int *total, Uint8 *mark)
{
	return glyph_matches_own(r, var, frame, pitch, total, mark, -1);
}

/* Notes this letter as the last to draw its pixels. */
static void mark_owner(const Rec *r, int self)
{
	const Sprite *sp = sprite(r->table, r->index);
	int px = r->x, py = r->y, xo = 0;
	for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
	{
		switch (*d)
		{
		case 255: d++; px += *d; xo += *d; break;
		case 254: px += sp->width - xo; xo = sp->width; break;
		case 253: px++; xo++; break;
		default:
			if (px >= 0 && px < W && py >= 0 && py < H)
				owner[r->layer][py][px] = self + 1;
			px++; xo++;
			break;
		}
		if (xo >= sp->width) { xo = 0; px -= sp->width; py++; }
	}
}

static void find_leaks(SDL_Surface *frame, const Uint8 *mask)
{
	static Uint8 leak[W * H];
	static unsigned long seen_sigs[512];
	static int n_sigs = 0, frames = 0, pictures = 0;
	if (!leak_env_read)
	{
		leak_env_read = true;
		leak_dir = getenv("MT_TEXT_LEAKS");
	}
	if (!leak_dir)
		return;
	frames++;
	memset(leak, 0, sizeof(leak));
	const Uint8 *px = frame->pixels;
	char desc[1024] = "";
	size_t dl = 0;
	unsigned long sig = 5381;
	int count = 0;
	for (int i = 0; i < MAX_RECS; i++)
	{
		Rec *r = &recs[i];
		if (!r->used || r->seen)
			continue;
		/* game_screen used as a scratch picture (ship specs) shows only after
		 * it is copied whole; until then its letters aren't on screen. */
		if (r->twin && !r->ever_whole)
			continue;
		int best = 0, total = 0, bk = -1;
		for (int k = 0; k < r->nv; k++)
		{
			int t, h = glyph_matches_own(r, &r->v[k], px, frame->pitch, &t, NULL, i);
			if (h > best) { best = h; total = t; bk = k; }
		}
		if (bk < 0 || best < 3 || best * 4 < total)
			continue;
		static Uint8 m[W * H];
		memset(m, 0, sizeof(m));
		int t;
		glyph_matches_own(r, &r->v[bk], px, frame->pitch, &t, m, i);
		int left = 0;
		for (int p = 0; p < W * H; p++)
			if (m[p] && !mask[p]) { leak[p] = 1; left++; }
		if (left < 4)
			continue;
		count++;
		sig = sig * 33 + (unsigned long)(r->y / 4) * 131 + (unsigned long)r->index;
		if (dl < sizeof(desc) - 40)
			dl += (size_t)snprintf(desc + dl, sizeof(desc) - dl, " covered:%c@%d,%d(%d/%d)",
			                       glyph_char[r->index] ? glyph_char[r->index] : '?', r->x, r->y, best, total);
	}
	for (int i = 0; i < n_odd; i++)
	{
		count++;
		sig = sig * 33 + (unsigned long)odd[i].why[0] * 7 + (unsigned long)(odd[i].y / 4);
		if (dl < sizeof(desc) - 40)
			dl += (size_t)snprintf(desc + dl, sizeof(desc) - dl, " %s:t%d#%d@%d,%d", odd[i].why,
			                       odd[i].table, odd[i].index, odd[i].x, odd[i].y);
		const Sprite *sp = sprite(odd[i].table, odd[i].index);
		for (int yy = odd[i].y; yy < odd[i].y + sp->height && yy < H; yy++)
			for (int xx = odd[i].x; xx < odd[i].x + sp->width && xx < W; xx++)
				if (yy >= 0 && xx >= 0) leak[yy * W + xx] = 1;
	}
	n_odd = 0;
	if (count == 0)
		return;
	for (int i = 0; i < n_sigs; i++)
		if (seen_sigs[i] == sig)
			return;
	if (n_sigs < 512)
		seen_sigs[n_sigs++] = sig;
	char path[600];
	snprintf(path, sizeof path, "%s/leaks.txt", leak_dir);
	FILE *f = fopen(path, "a");
	if (f)
	{
		fprintf(f, "frame %d: %d leaks%s\n", frames, count, desc);
		fclose(f);
	}
	if (pictures >= 200)
		return;
	pictures++;
	snprintf(path, sizeof path, "%s/leak_%05d.ppm", leak_dir, frames);
	f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6 %d %d 255\n", W, H);
	for (int p = 0; p < W * H; p++)
	{
		SDL_Color c = colors[px[(p / W) * frame->pitch + p % W]];
		Uint8 rgb[3] = { c.r, c.g, c.b };
		if (leak[p]) { rgb[0] = 255; rgb[1] = 0; rgb[2] = 255; }
		else if (mask[p]) { rgb[0] = rgb[1] = rgb[2] = 90; }
		fwrite(rgb, 1, 3, f);
	}
	fclose(f);
}

static int record_glyph(int layer, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value, bool unsafe);

void mt_text_glyph(SDL_Surface *surface, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value, bool unsafe)
{
	if (!is_font_table(table) || index >= 128)
		return;
	prepare_tables();
	int alt = -1;
	if (surface == game_screen && glyph_char[index] != 0)
		alt = record_glyph(1, x, y, table, index, hue, value, unsafe);
	to_screen(surface, &x, &y);
	if (glyph_char[index] == 0)
	{
		note_odd(x, y, table, index, "unmapped");
		return;  /* not a plain ASCII letter: leave it as pixels */
	}
	int main = record_glyph(0, x, y, table, index, hue, value, unsafe);
	if (main >= 0)
		recs[main].twin = alt >= 0 ? alt + 1 : 0;
	if (alt >= 0)
		recs[alt].twin = main >= 0 ? main + 1 : 0;
}

static int record_glyph(int layer, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value, bool unsafe)
{
	if (x < 0 || y < 0 || x >= W || y >= H)
		return -1;

	int slot = grid[layer][y][x] - 1;
	if (slot < 0)
	{
		for (int tries = 0; tries < MAX_RECS; tries++)
		{
			int i = (next_free + tries) % MAX_RECS;
			if (!recs[i].used)
			{
				slot = i;
				next_free = (i + 1) % MAX_RECS;
				break;
			}
		}
		if (slot < 0)
		{
			/* Full: reuse the record unseen for longest. */
			slot = 0;
			for (int i = 1; i < MAX_RECS; i++)
				if (recs[i].misses > recs[slot].misses) slot = i;
			grid[recs[slot].layer][recs[slot].y][recs[slot].x] = 0;
		}
		grid[layer][y][x] = slot + 1;
	}
	Rec *r = &recs[slot];
	if (!r->used || r->table != table || r->index != index)
	{
		memset(r, 0, sizeof(*r));
		r->used = true;
		r->x = x; r->y = y;
		r->table = table; r->index = index;
		r->layer = layer;
	}
	/* This way of drawing goes to the front of the list. */
	Variant nv = { hue, value, unsafe };
	int k = 0;
	while (k < r->nv && !(r->v[k].hue == hue && r->v[k].value == value && r->v[k].unsafe == unsafe))
		k++;
	if (k == r->nv && r->nv < VARIANTS)
		r->nv++;
	if (k == VARIANTS)
		k = VARIANTS - 1;
	memmove(&r->v[1], &r->v[0], (size_t)k * sizeof(Variant));
	r->v[0] = nv;
	r->misses = 0;

	/* Darker copies of this letter just drawn around it are its shadow or
	 * outline: shadows (blit_sprite_dark) at any offset up to 2 pixels, and
	 * the same letter drawn much darker (outlines in the title menu, glowing
	 * level text). Those darker letters are not text of their own. */
	for (int i = 0; i < RECENT; i++)
	{
		int dx = recent[i].x - x, dy = recent[i].y - y;
		if (recent[i].layer != layer || recent[i].table != table || recent[i].index != index ||
		    dx < -2 || dx > 2 || dy < -2 || dy > 2 || (dx == 0 && dy == 0))
			continue;
		Uint32 bit = 1u << ((dy + 2) * 5 + (dx + 2));
		if (recent[i].kind == DRAW_DARK)
			r->outline_dark |= bit;
		else if (recent[i].kind == DRAW_BLACK)
			r->outline_black |= bit;
		else if (recent[i].value <= value - 4)
		{
			r->outline_black |= bit;
			r->outline_coloured = true;
			r->outline_value = recent[i].value;
			forget(layer, x + dx, y + dy, table, index);
		}
	}
	remember(layer, table, index, DRAW_LETTER, value, x, y);
	mark_owner(r, slot);

	/* Text drawn over other text replaces it: forget letters whose cells this
	 * one overlaps (a new menu title over the old one, a changed number). */
	const Sprite *sp = sprite(table, index);
	for (int yy = y - 16; yy < y + sp->height; yy++)
	{
		if (yy < 0 || yy >= H)
			continue;
		for (int xx = x - 16; xx < x + sp->width; xx++)
		{
			if (xx < 0 || xx >= W || (xx == x && yy == y))
				continue;
			int o = grid[layer][yy][xx] - 1;
			if (o < 0)
				continue;
			const Sprite *os = sprite(recs[o].table, recs[o].index);
			/* Overlapping by at least half the smaller cell: a descender
			 * reaching into the next line doesn't count. */
			int ow = (xx + os->width < x + sp->width ? xx + os->width : x + sp->width) - (xx > x ? xx : x);
			int oh = (yy + os->height < y + sp->height ? yy + os->height : y + sp->height) - (yy > y ? yy : y);
			int small = os->width * os->height < sp->width * sp->height ? os->width * os->height : sp->width * sp->height;
			if (ow > 0 && oh > 0 && ow * oh * 2 >= small)
			{
				recs[o].used = false;
				grid[layer][yy][xx] = 0;
			}
		}
	}
	return slot;
}

/* Walks a glyph's pixels as blit_sprite_hv does. If mask is NULL, returns
 * whether every pixel is in the frame as drawn; otherwise marks the pixels. */
static bool glyph_walk(const Rec *r, const Variant *var, int dx, int dy, const Uint8 *frame, int pitch, Uint8 *mask)
{
	const Sprite *sp = sprite(r->table, r->index);
	const Uint8 hue = var->hue << 4;
	int px = r->x + dx, py = r->y + dy, xo = 0;
	const int width = sp->width;
	for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
	{
		switch (*d)
		{
		case 255: d++; px += *d; xo += *d; break;
		case 254: px += width - xo; xo = width; break;
		case 253: px++; xo++; break;
		default:
		{
			Uint8 v;
			if (var->unsafe)
				v = hue | (Uint8)((*d & 0x0f) + var->value);
			else
			{
				Uint8 t = (*d & 0x0f) + var->value;
				if (t > 0xf)
					t = (t >= 0x1f) ? 0x0 : 0xf;
				v = hue | t;
			}
			if (px >= 0 && px < W && py >= 0 && py < H)
			{
				if (mask)
					mask[py * W + px] = 1;
				else if (frame[py * pitch + px] != v)
					return false;
			}
			px++; xo++;
			break;
		}
		}
		if (xo >= width)
		{
			xo = 0;
			px -= width;
			py++;
		}
	}
	return true;
}

static bool green(Uint8 index)
{
	SDL_Color c = colors[index];
	return c.g > 30 && c.g > c.r + 20 && c.g > c.b + 20;
}

void mt_text_clear_labels(void)
{
	memset(labels, 0, sizeof(labels));
}

void mt_text_label_pixels(Uint8 *mask)
{
	for (int i = 0; i < MAX_LABELS; i++)
	{
		const Label *l = &labels[i];
		if (!l->used)
			continue;
		const int bw = l->x1 - l->x0;
		for (int y = l->y0; y < l->y1; y++)
			for (int x = l->x0; x < l->x1; x++)
				if (x >= 0 && x < W && y >= 0 && y < H && l->is_text[(y - l->y0) * bw + (x - l->x0)])
					mask[y * W + x] = 1;
	}
}

void mt_text_label(SDL_Surface *surface, int x0, int y0, int x1, int y1, const char *text, bool vertical)
{
	if (x0 < 0 || y0 < 0 || x1 > W || y1 > H || x1 - x0 > 64 || y1 - y0 > 24 || x1 <= x0 || y1 <= y0)
		return;
	/* A label over the same spot replaces the old one. */
	int slot = -1;
	for (int i = 0; i < MAX_LABELS; i++)
		if (labels[i].used && labels[i].x0 < x1 && x0 < labels[i].x1 && labels[i].y0 < y1 && y0 < labels[i].y1)
			labels[i].used = false;
	for (int i = 0; i < MAX_LABELS && slot < 0; i++)
		if (!labels[i].used) slot = i;
	if (slot < 0)
		return;
	Label *l = &labels[slot];
	memset(l, 0, sizeof(*l));
	l->used = true; l->vertical = vertical;
	l->x0 = x0; l->y0 = y0; l->x1 = x1; l->y1 = y1;
	strncpy(l->text, text, sizeof(l->text) - 1);
	unsigned counts[256] = { 0 };
	const int bw = x1 - x0;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
		{
			Uint8 v = ((Uint8 *)surface->pixels)[y * surface->pitch + x];
			l->pixels[(y - y0) * bw + (x - x0)] = v;
			if (green(v))
			{
				l->is_text[(y - y0) * bw + (x - x0)] = 1;
				counts[v]++;
			}
		}
	unsigned best = 0;
	for (int i = 1; i < 256; i++)
		if (counts[i] > counts[best]) best = i;
	l->hue = best >> 4;
	l->level = best & 0x0f;
}

static const char *const icon_words[] = {
	/* The icons' small raised dot is drawn as a hyphen (Orbitron has no
	 * middle dot). */
	"MONO", "DUAL", "VULCAN", "MEGA-M", "MINI-M", "BLAST", "WOBBLE", "PHOENX",
	"A-BOMB", "CHARGE", "ZICA", "BUSTER" };

void mt_text_icon_word(SDL_Surface *surface, int x, int y, unsigned int icon)
{
	const char *word = NULL;
	if (icon < COUNTOF(icon_words)) word = icon_words[icon];
	else if (icon == 37) word = "STORM";
	else if (icon == 38) word = "M-BOMB";
	if (word == NULL || !sprite_exists(OPTION_SHAPES, icon))
		return;
	const Sprite *sp = sprite(OPTION_SHAPES, icon);
	/* The word is the green lettering in the icon's top rows, down to the
	 * first row without any. */
	int top = -1, bottom = -1, left = W, right = -1;
	for (int yy = y; yy < y + sp->height && yy < H; yy++)
	{
		bool any = false;
		for (int xx = x; xx < x + sp->width && xx < W; xx++)
			if (green(((Uint8 *)surface->pixels)[yy * surface->pitch + xx]))
			{
				any = true;
				if (xx < left) left = xx;
				if (xx > right) right = xx;
			}
		if (any && top < 0) top = yy;
		if (!any && top >= 0) { bottom = yy; break; }
	}
	if (top < 0 || bottom < 0 || right < left)
		return;
	mt_text_label(surface, left, top, right + 1, bottom, word, false);
}

/* The mouse pointer. While it is in VGAScreen, frames are handed over without
 * it (the pixels it covers put back) and the pointer goes separately, so the
 * app can draw it over the crisp text, and letters under it still count. */
static struct
{
	bool drawn, lifted;
	int x, y;
	Uint8 under[MT_CURSOR_W * MT_CURSOR_H];
	mt_text_cursor layer;
}
cursor;

void mt_text_cursor_drawn(int x, int y, const Uint8 *under)
{
	cursor.drawn = true;
	cursor.x = x;
	cursor.y = y;
	memcpy(cursor.under, under, sizeof(cursor.under));
}

void mt_text_cursor_removed(void)
{
	cursor.drawn = false;
}

/* Swaps the pixels under the pointer box with what is stored. The grab and
 * restore in mouse.c run through memory in the same row-wrapping way. */
static void cursor_swap(SDL_Surface *frame, Uint8 *store)
{
	Uint8 *px = frame->pixels;
	for (int yy = 0; yy < MT_CURSOR_H; yy++)
		for (int xx = 0; xx < MT_CURSOR_W; xx++)
		{
			long at = (long)(cursor.y + yy) * frame->pitch + cursor.x + xx;
			if (at < 0 || at >= (long)frame->pitch * frame->h)
				continue;
			Uint8 t = px[at];
			px[at] = store[yy * MT_CURSOR_W + xx];
			store[yy * MT_CURSOR_W + xx] = t;
		}
}

void mt_text_presented(SDL_Surface *frame)
{
	if (cursor.lifted)
	{
		cursor_swap(frame, cursor.layer.pixels);   /* the pointer back in */
		cursor.lifted = false;
	}
}

void mt_text_present(SDL_Surface *frame)
{
	static mt_text_item items[MT_TEXT_MAX];
	static Uint8 mask[W * H];
	if (mt_setting(MT_SETTING_TEXT) != 0)
	{
		mt_set_frame_text(NULL, 0, NULL, NULL);
		return;
	}
	prepare_tables();
	memset(mask, 0, sizeof(mask));

	cursor.layer.shown = 0;
	if (cursor.drawn && frame == VGAScreen)
	{
		/* Lift the pointer out: the frame gets the pixels from under it. */
		memcpy(cursor.layer.pixels, cursor.under, sizeof(cursor.under));
		cursor_swap(frame, cursor.layer.pixels);
		cursor.lifted = true;
		for (int i = 0; i < MT_CURSOR_W * MT_CURSOR_H; i++)
			cursor.layer.opaque[i] = cursor.layer.pixels[i] != cursor.under[i];
		cursor.layer.x = cursor.x;
		cursor.layer.y = cursor.y;
		cursor.layer.shown = 1;
	}
	int n = 0;
	const Uint8 *px = frame->pixels;
	const int pitch = frame->pitch;

	/* A new screen (most pixels changed): letters seen before must show whole
	 * again before part of one counts, so leftovers of the old screen that
	 * happen to match a few pixels of the new one don't. */
	{
		static Uint8 prev[W * H];
		int changed = 0;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				const Uint8 v = px[y * pitch + x];
				changed += prev[y * W + x] != v;
				prev[y * W + x] = v;
			}
		if (changed * 10 > W * H * 4)
			for (int i = 0; i < MAX_RECS; i++)
				recs[i].ever_whole = false;
	}

	/* Which letters are in the frame whole. For game_screen letters noted on
	 * both layers, the layer most of them match on wins this frame; a twin
	 * on the other layer matching too is a coincidence. */
	int layer_votes[2] = { 0, 0 };
	for (int i = 0; i < MAX_RECS; i++)
	{
		Rec *r = &recs[i];
		r->whole_k = -1;
		if (!r->used)
			continue;
		int k = 0;
		while (k < r->nv && !glyph_walk(r, &r->v[k], 0, 0, px, pitch, NULL))
			k++;
		/* A look so dark that none of its solid body shows (the darkest step
		 * of flashing text, "Secret Level!") proves nothing: its few faint
		 * pixels can sit in the scenery by chance, and it would stay. */
		if (k < r->nv)
		{
			int body;
			glyph_matches(r, &r->v[k], px, pitch, &body, NULL);
			if (body == 0)
				k = r->nv;
		}
		if (k < r->nv)
		{
			r->whole_k = k;
			if (r->twin)
				layer_votes[r->layer]++;
		}
	}
	const int losing_layer = layer_votes[1] > layer_votes[0] ? 0 : 1;

	for (int i = 0; i < MAX_RECS; i++)
	{
		Rec *r = &recs[i];
		if (!r->used)
			continue;
		if (r->twin && r->layer == losing_layer && recs[r->twin - 1].used &&
		    recs[r->twin - 1].twin == i + 1 &&
		    recs[r->twin - 1].whole_k >= 0)
		{
			r->seen = false;
			continue;
		}
		int k = r->whole_k >= 0 ? r->whole_k : r->nv;
		bool partly = false;
		int clip_x0 = -1, clip_x1 = -1;
		int clip_y0 = -1, clip_y1 = -1;   /* rows shown, when cut off */
		if (k == r->nv)
		{
			/* Something drawn over part of the letter (a shot, an explosion):
			 * if half of it or more is still there, it still counts, so it doesn't
			 * flicker back to the old font. */
			int best = 0, total = 0;
			for (int j = 0; j < r->nv; j++)
			{
				int t, h = glyph_matches_own(r, &r->v[j], px, pitch, &t, NULL, i);
				if (h > best) { best = h; total = t; k = j; }
			}
			/* Seen whole before: half is enough. Never seen whole (drawn
			 * and covered in the same frame): most of it must be there. */
			if (total > 0 && r->ever_whole && best >= 3 && best * 4 >= total)
			{
				/* Seen whole before: whatever part still shows counts, and
				 * the crisp letter is cut to that part (a window sliding over
				 * it, an explosion in front of it). */
				glyph_matches_own(r, &r->v[k], px, pitch, &total, NULL, i);
				partly = true;
				const Sprite *gs = sprite(r->table, r->index);
				if (hit_x0 > r->x) clip_x0 = hit_x0;
				if (hit_x1 < r->x + gs->width) clip_x1 = hit_x1;
				if (hit_y0 > r->y) clip_y0 = hit_y0;
				if (hit_y1 < r->y + gs->height) clip_y1 = hit_y1;
			}
			else if (total > 0 && best * 5 >= total * 4)
				partly = true;
			else if (total > 0 && best >= 3)
			{
				/* Cut off at the top or bottom (scrolling text inside a box):
				 * the rows still shown are all there, the rest are gone. */
				glyph_matches_own(r, &r->v[k], px, pitch, &total, NULL, i);
				const int gh = sprite(r->table, r->index)->height;
				int a = 0, b = gh;
				while (a < gh && row_hit[a] == 0) a++;
				while (b > a && row_hit[b - 1] == 0) b--;
				int in_tot = 0, in_hit = 0;
				for (int g = a; g < b; g++) { in_tot += row_tot[g]; in_hit += row_hit[g]; }
				if (in_tot > 0 && in_hit * 10 >= in_tot * 9 && (a > 0 || b < gh))
				{
					partly = true;
					clip_y0 = a > 0 ? r->y + a : -1;
					clip_y1 = b < gh ? r->y + b : -1;
				}
				else
					k = r->nv;
			}
			else
				k = r->nv;
		}
		if (k < r->nv && !partly)
			r->ever_whole = true;
		r->seen = k < r->nv;
		if (k == r->nv)
		{
			if (++r->misses > MISS_LIMIT)
			{
				r->used = false;
				grid[r->layer][r->y][r->x] = 0;
			}
			continue;
		}
		r->misses = 0;
		if (n >= MT_TEXT_MAX || glyph_char[r->index] == 0)
			continue;            /* no crisp version: leave the original pixels */
		const Variant *var = &r->v[k];
		int unused;
		if (partly)
			glyph_matches_own(r, var, px, pitch, &unused, mask, i);   /* only what is still there */
		else
			glyph_walk(r, var, 0, 0, px, pitch, mask);
		/* The old shadows and outlines go too; the crisp letter brings its own. */
		const Uint32 outline = partly ? 0 : r->outline_dark | r->outline_black;
		for (int b = 0; b < 25; b++)
			if (outline & (1u << b))
				glyph_walk(r, var, b % 5 - 2, b / 5 - 2, px, pitch, mask);
		const Sprite *sp = sprite(r->table, r->index);
		mt_text_item *it = &items[n++];
		memset(it, 0, sizeof(*it));
		it->x = r->x; it->y = r->y;
		it->w = sp->width; it->h = sp->height;
		it->cap = cap_height[r->table];
		it->kind = MT_TEXT_LETTER;
		it->hue = var->hue & 0x0f;
		int level = body_level[r->table] + var->value;
		it->level = level < 0 ? 0 : level > 15 ? 15 : level;
		it->clip_y0 = clip_y0;
		it->clip_y1 = clip_y1;
		it->clip_x0 = clip_x0;
		it->clip_x1 = clip_x1;
		it->outline_dark = r->outline_dark;
		if (r->outline_coloured)
		{
			int ol = body_level[r->table] + r->outline_value;
			it->outline_level = ol < 0 ? 0 : ol > 15 ? 15 : ol;
		}
		else
			it->outline_level = 255;
		it->outline_black = r->outline_black;
		it->font = r->table;
		it->text[0] = (char)glyph_char[r->index];
	}

	for (int i = 0; i < MAX_LABELS; i++)
	{
		Label *l = &labels[i];
		if (!l->used || n >= MT_TEXT_MAX)
			continue;
		const int bw = l->x1 - l->x0;
		bool same = true;
		for (int y = l->y0; y < l->y1 && same; y++)
			for (int x = l->x0; x < l->x1; x++)
				if (px[y * pitch + x] != l->pixels[(y - l->y0) * bw + (x - l->x0)])
				{
					same = false;
					break;
				}
		if (!same)
			continue;
		for (int y = l->y0; y < l->y1; y++)
			for (int x = l->x0; x < l->x1; x++)
				if (l->is_text[(y - l->y0) * bw + (x - l->x0)])
					mask[y * W + x] = 1;
		mt_text_item *it = &items[n++];
		memset(it, 0, sizeof(*it));
		it->x = l->x0; it->y = l->y0;
		it->w = bw; it->h = l->y1 - l->y0;
		it->cap = it->h;
		it->kind = l->vertical ? MT_TEXT_LABEL_V : MT_TEXT_LABEL;
		it->hue = l->hue; it->level = l->level;
		it->font = 3;
		it->clip_y0 = it->clip_y1 = it->clip_x0 = it->clip_x1 = -1;
		strncpy(it->text, l->text, sizeof(it->text) - 1);
	}

	find_leaks(frame, mask);
	mt_set_frame_text(items, n, mask, cursor.layer.shown ? &cursor.layer : NULL);
}

#endif /* METALTYRIAN */
