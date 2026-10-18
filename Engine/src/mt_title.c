/*
 * MetalTyrian: high-resolution title screen and pictures (see mt_title.h). Not part of
 * OpenTyrian.
 */
#ifdef METALTYRIAN

#include "mt_title.h"

#include "palette.h"
#include "sprite.h"
#include "video.h"

#include "metaltyrian_host.h"

#include "mt_text.h"
#include "pcxmast.h"

#include <string.h>

#define W 320
#define H 200

static const unsigned logo_sprite[2] = { 146, 151 };   /* PLANET_SHAPES */

static struct
{
	bool loaded;
	Uint8 picture[W * H];   /* the title picture as loaded */
	Uint32 brightness;      /* sum of its palette, entries 0-239 */
	bool logo_drawn[2];
	int logo_x[2], logo_y[2];
}
title;

void mt_title_picture(SDL_Surface *surface)
{
	for (int y = 0; y < H; y++)
		memcpy(title.picture + y * W, (Uint8 *)surface->pixels + y * surface->pitch, W);
	title.brightness = 0;
	for (int i = 0; i < 240; i++)
		title.brightness += colors[i].r + colors[i].g + colors[i].b;
	title.loaded = true;
	title.logo_drawn[0] = title.logo_drawn[1] = false;
}

void mt_title_logo(int which, int x, int y)
{
	if (which < 0 || which > 1)
		return;
	title.logo_drawn[which] = true;
	title.logo_x[which] = x;
	title.logo_y[which] = y;
}

/* Walks a logo's opaque pixels at its last position: counts those the frame
 * shows as drawn, and marks them in cover. */
static int logo_walk(int which, const Uint8 *frame, int pitch, Uint8 *cover, int *total)
{
	const Sprite *sp = sprite(PLANET_SHAPES, logo_sprite[which]);
	int px = title.logo_x[which], py = title.logo_y[which], xo = 0, hits = 0;
	*total = 0;
	for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
	{
		switch (*d)
		{
		case 255: d++; px += *d; xo += *d; break;
		case 254: px += sp->width - xo; xo = sp->width; break;
		case 253: px++; xo++; break;
		default:
			if (px >= 0 && px < W && py >= 0 && py < H)
			{
				(*total)++;
				if (frame[py * pitch + px] == *d)
				{
					hits++;
					if (cover) cover[py * W + px] = 1;
				}
			}
			px++; xo++;
			break;
		}
		if (xo >= sp->width) { xo = 0; px -= sp->width; py++; }
	}
	return hits;
}

static mt_title_frame t;
static Uint8 foreground[W * H];

static Uint32 rgb_sum(Uint32 c)
{
	return ((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF);
}

static bool title_frame(SDL_Surface *frame)
{
	if (!title.loaded)
		return false;
	const Uint8 *px = frame->pixels;
	const int pitch = frame->pitch;

	/* Still the title screen: a good part of the picture shows as loaded. */
	int same = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			same += px[y * pitch + x] == title.picture[y * W + x];
	if (same * 10 < W * H * 3)
		return false;

	static Uint8 cover[W * H];
	memset(cover, 0, sizeof(cover));
	for (int l = 0; l < 2; l++)
	{
		if (!title.logo_drawn[l])
			continue;
		int total, hits = logo_walk(l, px, pitch, NULL, &total);
		if (total == 0 || hits * 10 < total * 6)
			continue;
		logo_walk(l, px, pitch, cover, &total);
		const Sprite *sp = sprite(PLANET_SHAPES, logo_sprite[l]);
		t.logo_shown[l] = 1;
		t.logo_x[l] = title.logo_x[l];
		t.logo_y[l] = title.logo_y[l];
		t.logo_w[l] = sp->width;
		t.logo_h[l] = sp->height;
	}

	/* Everything else drawn on the picture (menu, version, pointer) stays. */
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			foreground[y * W + x] = !cover[y * W + x] && px[y * pitch + x] != title.picture[y * W + x];
	t.shown = 1;
	t.brightness = title.brightness;
	Uint32 sum = 0;
	for (int i = 0; i < 240; i++)
		sum += rgb_sum(rgb_palette[i]);
	t.fade = title.brightness == 0 || sum >= title.brightness ? 4096 : (Uint16)((Uint64)sum * 4096 / title.brightness);
	return true;
}

/* ---- The other pictures ---------------------------------------------------- */

static struct
{
	bool loaded;
	unsigned stamp;           /* load order, newest highest */
	Uint8 pixels[W * H];      /* as loaded (plus mt_picture_sprite) */
	Uint8 common;             /* its most used colour: its plain background */
	Uint32 hist[256];         /* colour use outside the hole, for the fade */
	Uint64 brightness;        /* the colours' sum in the picture's own palette */
}
pics[PCX_NUM + 1];
static unsigned load_count;
static int current;           /* the picture the last frame showed, 0 none */

/* The HUD pictures leave the playfield to the game. */
static SDL_Rect hole_of(int n)
{
	if (n == 3 || n == 6)
		return (SDL_Rect){ 0, 0, 264, 184 };
	return (SDL_Rect){ 0, 0, 0, 0 };
}

static bool in_hole(const SDL_Rect *h, int x, int y)
{
	return x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h;
}

static void picture_stats(int n)
{
	const SDL_Rect hole = hole_of(n);
	memset(pics[n].hist, 0, sizeof(pics[n].hist));
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if (!in_hole(&hole, x, y))
				pics[n].hist[pics[n].pixels[y * W + x]]++;
	int best = 0;
	for (int i = 1; i < 256; i++)
		if (pics[n].hist[i] > pics[n].hist[best]) best = i;
	pics[n].common = (Uint8)best;
}

void mt_picture_loaded(SDL_Surface *surface, int number)
{
	if (number < 1 || number > PCX_NUM)
		return;
	for (int y = 0; y < H; y++)
		memcpy(pics[number].pixels + y * W, (Uint8 *)surface->pixels + y * surface->pitch, W);
	picture_stats(number);
	/* JE_loadPic has just set colors to the picture's own palette. */
	pics[number].brightness = 0;
	for (int i = 0; i < 256; i++)
		pics[number].brightness += (Uint64)pics[number].hist[i] * (colors[i].r + colors[i].g + colors[i].b);
	pics[number].loaded = true;
	pics[number].stamp = ++load_count;
}

void mt_picture_sprite(int number, unsigned table, unsigned index, int x, int y)
{
	if (number < 1 || number > PCX_NUM || !pics[number].loaded || !sprite_exists(table, index))
		return;
	const Sprite *sp = sprite(table, index);
	int px = x, py = y, xo = 0;
	for (const Uint8 *d = sp->data; d < sp->data + sp->size; d++)
	{
		switch (*d)
		{
		case 255: d++; px += *d; xo += *d; break;
		case 254: px += sp->width - xo; xo = sp->width; break;
		case 253: px++; xo++; break;
		default:
			if (px >= 0 && px < W && py >= 0 && py < H)
				pics[number].pixels[py * W + px] = *d;
			px++; xo++;
			break;
		}
		if (xo >= sp->width) { xo = 0; px -= sp->width; py++; }
	}
}

/* Whether the frame shows picture n: enough of it as loaded, counting its
 * plain background and the rest apart, so a dark screen isn't taken for a
 * mostly dark picture. */
static bool shows(int n, const Uint8 *px, int pitch)
{
	const SDL_Rect hole = hole_of(n);
	const Uint8 *pic = pics[n].pixels;
	int same = 0, total = 0, detail = 0, detail_same = 0;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			if (in_hole(&hole, x, y))
				continue;
			const Uint8 v = pic[y * W + x];
			const bool eq = px[y * pitch + x] == v;
			total++;
			same += eq;
			if (v != pics[n].common)
			{
				detail++;
				detail_same += eq;
			}
		}
	return total > 0 && same * 10 >= total * 3 && detail > 0 && detail_same * 10 >= detail * 3;
}

static bool picture_frame(SDL_Surface *frame)
{
	const Uint8 *px = frame->pixels;
	const int pitch = frame->pitch;
	int found = 0;
	if (current && pics[current].loaded && shows(current, px, pitch))
		found = current;
	if (!found)
	{
		/* Candidates newest first, at most four checked. */
		int order[PCX_NUM], count = 0;
		for (int n = 1; n <= PCX_NUM; n++)
			if (n != 4 && n != current && pics[n].loaded)
				order[count++] = n;
		for (int i = 1; i < count; i++)
			for (int j = i; j > 0 && pics[order[j]].stamp > pics[order[j - 1]].stamp; j--)
			{
				int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp;
			}
		for (int i = 0; i < count && i < 4 && !found; i++)
			if (shows(order[i], px, pitch))
				found = order[i];
	}
	current = found;
	if (!found)
		return false;

	const SDL_Rect hole = hole_of(found);
	const Uint8 *pic = pics[found].pixels;
	/* What the game drew over the picture: 1 = its own pixel replaces the
	 * art; 2-255 = the picture shaded darker in place (menu highlight bars,
	 * shaded boxes: same colour, lower brightness), which the app applies to
	 * the art as a darkening instead, so the art's detail shows through. */
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			const Uint8 f = px[y * pitch + x], o = pic[y * W + x];
			Uint8 v = 0;
			if (!in_hole(&hole, x, y) && f != o)
			{
				v = 1;
				const Uint32 fs = rgb_sum(rgb_palette[f]), os = rgb_sum(rgb_palette[o]);
				if ((f >> 4) == (o >> 4) && fs < os && os > 0)
				{
					const Uint32 dark = 253 - fs * 253 / os;   /* 0 = as bright, 253 = black */
					v = (Uint8)(2 + dark);
				}
				else if (fs == 0 && os > 0)
					v = 255;   /* shaded to black */
			}
			foreground[y * W + x] = v;
		}
	/* The new HUD art has no painted labels: crisp text draws them, or with
	 * crisp text off, the game's own lettering stays on top. */
	if ((found == 3 || found == 6) && mt_setting(MT_SETTING_TEXT) != 0)
		mt_text_label_pixels(foreground);
	Uint64 sum = 0;
	for (int i = 0; i < 256; i++)
		if (pics[found].hist[i])
			sum += (Uint64)pics[found].hist[i] * rgb_sum(rgb_palette[i]);
	t.shown = 1;
	t.picture = (Uint8)found;
	t.fade = pics[found].brightness == 0 || sum >= pics[found].brightness ? 4096 : (Uint16)(sum * 4096 / pics[found].brightness);
	t.smooth = found == 3 || found == 6;
	t.hole[0] = (Sint16)hole.x; t.hole[1] = (Sint16)hole.y; t.hole[2] = (Sint16)hole.w; t.hole[3] = (Sint16)hole.h;
	return true;
}

void mt_title_present(SDL_Surface *frame)
{
	memset(&t, 0, sizeof(t));
	if (frame != VGAScreen || !(title_frame(frame) || picture_frame(frame)))
	{
		mt_set_frame_title(NULL, NULL);
		return;
	}
	mt_set_frame_title(&t, foreground);
}

#endif /* METALTYRIAN */
