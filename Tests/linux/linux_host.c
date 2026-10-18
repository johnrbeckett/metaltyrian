/* Headless Linux host for testing the engine on the SDL shim. Pulls frames
 * like the app does, and pulls sound in real time into a WAV file when
 * MT_WAV is set. */
#include "metaltyrian_host.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile int running = 1;
extern uint8_t displayTime;   /* the game's "Secret Level!" banner timer (varz.c) */

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void *audio_thread(void *arg)
{
	const char *path = arg;
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return NULL; }
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32(f, 16); put16(f, 1); put16(f, 2); put32(f, MT_AUDIO_RATE); put32(f, MT_AUDIO_RATE * 4);
	put16(f, 4); put16(f, 16);
	fwrite("data\0\0\0\0", 1, 8, f);
	uint32_t bytes = 0;
	enum { CHUNK = MT_AUDIO_RATE / 100 };  /* 10 ms, like a small Core Audio buffer */
	float bl[CHUNK], br[CHUNK];
	int16_t pcm[CHUNK * 2];
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (running)
	{
		mt_audio_render(bl, br, CHUNK);
		for (int i = 0; i < CHUNK * 2; i++)
		{
			float v = (i & 1 ? br[i / 2] : bl[i / 2]) * 32768.0f;
			pcm[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
		}
		fwrite(pcm, 2, CHUNK * 2, f);
		bytes += CHUNK * 4;
		next.tv_nsec += 10000000;
		if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}
	fseek(f, 4, SEEK_SET); put32(f, 36 + bytes);
	fseek(f, 40, SEEK_SET); put32(f, bytes);
	fclose(f);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *gargv[] = { "metaltyrian", "-t", argv[1] };
	mt_start_game(3, gargv);
	mt_push_focus(true);   /* as the app's window does, or the game pauses itself */
	pthread_t at;
	const char *wav = getenv("MT_WAV");
	if (wav)
		pthread_create(&at, NULL, audio_thread, (void *)wav);
	int seconds = argc > 2 ? atoi(argv[2]) : 100;
	static uint8_t px[320 * 200]; static uint32_t pal[256];
	uint64_t last = 0;
	for (int i = 0; i < seconds * 10 && !mt_game_finished(); ++i)
	{
		usleep(100000);
		/* MT_KEYS="tick:scancode,...": press a key at that tick, release it on the next (10 a second). */
		/* MT_MOUSE="tick:x:y,...": move the mouse there at that tick. */
		for (const char *k = getenv("MT_MOUSE"); k && *k;)
		{
			int tick = 0, mx = 0, my = 0;
			if (sscanf(k, "%d:%d:%d", &tick, &mx, &my) == 3 && tick == i)
				mt_push_mouse_motion(mx, my, 1, 1);
			k = strchr(k, ',');
			if (k) k++;
		}
		/* MT_SCRIPT="w:Text;k:40;p:5;d;...": steps run in order. w waits (up to
		 * 40 s) until the crisp text on screen, letters joined without spaces,
		 * contains Text; k presses a key (scancode), released next tick; p
		 * pauses N ticks; d dumps the frame. */
		static int step_pos = 0, pause = 0, waited = 0, release = 0, dump_now = 0;
		static char screen_text[8192] = "";
		static mt_title_frame title;
		if (release) { mt_push_key(release, false, 0); release = 0; }
		const char *script = getenv("MT_SCRIPT");
		while (script && script[step_pos] && pause == 0 && !release)
		{
			const char *st = script + step_pos;
			const char *end = strchr(st, ';');
			int len = end ? (int)(end - st) : (int)strlen(st);
			char arg[256] = "";
			if (len > 2) { memcpy(arg, st + 2, (size_t)(len - 2 < 255 ? len - 2 : 255)); arg[len - 2 < 255 ? len - 2 : 255] = 0; }
			int next = step_pos + len + (end ? 1 : 0);
			if (st[0] == 'w')
			{
				if (strstr(screen_text, arg) || ++waited > 400)
				{
					if (waited > 400) printf("script: gave up waiting for %s (screen: %.200s)\n", arg, screen_text);
					waited = 0; step_pos = next; pause = 3;
				}
				break;
			}
			else if (st[0] == 'k') { int code = atoi(arg); mt_push_key(code, true, 0); release = code; step_pos = next; pause = 4; break; }
			else if (st[0] == 's') { displayTime = 150; step_pos = next; }   /* s: show "Secret Level!" */
			else if (st[0] == 'p') { pause = atoi(arg); step_pos = next; break; }
			else if (st[0] == 'd') { dump_now = 1; step_pos = next; printf("script: tick %d pic %d fade %d smooth %d title %d logos %d@%d,%d %d@%d,%d screen: %.300s\n", i, title.shown ? title.picture : -1, title.fade, title.smooth, title.shown, title.logo_shown[0], title.logo_x[0], title.logo_y[0], title.logo_shown[1], title.logo_x[1], title.logo_y[1], screen_text); }
			else step_pos = next;
		}
		if (pause > 0) pause--;
		if (script && !script[step_pos] && pause == 0 && !release && !getenv("MT_SCRIPT_KEEP"))
		{
			printf("script done at tick %d\n", i);
			break;
		}
		const char *keys = getenv("MT_KEYS");
		for (const char *k = keys; k && *k;)
		{
			int tick = 0, code = 0;
			if (sscanf(k, "%d:%d", &tick, &code) == 2 && tick == i)
				mt_push_key(code, true, 0);
			else if (tick == i - 1)
				mt_push_key(code, false, 0);
			k = strchr(k, ',');
			if (k) k++;
		}
		if (getenv("MT_TEXT_LOG"))
		{
			static mt_text_item items[MT_TEXT_MAX];
			static uint8_t mask[320 * 200], title_fg[320 * 200];
			int count = 0;
			uint64_t n = mt_copy_frame_with_text(last, px, pal, items, MT_TEXT_MAX, &count, mask, NULL, &title, title_fg);
			const char *dump = getenv("MT_TEXT_DUMP");
			const int every = getenv("MT_TEXT_EVERY") ? atoi(getenv("MT_TEXT_EVERY")) : 10;
			if (n != last)
			{
				/* Which high-resolution picture the app would show. */
				static int last_pic = -2;
				const int pic = title.shown ? title.picture : -1;
				if (pic != last_pic || (getenv("MT_PIC_EVERY") && i % atoi(getenv("MT_PIC_EVERY")) == 0))
				{
					int fg = 0;
					for (int k = 0; k < 64000; k++) fg += title.shown && title_fg[k];
					printf("picture: tick %d -> %d (fade %d smooth %d hole %d,%d %dx%d, %d pixels on top)\n", i, pic, title.fade,
					       title.smooth, title.hole[0], title.hole[1], title.hole[2], title.hole[3], fg);
					last_pic = pic;
				}
				if (dump_now && getenv("MT_PIC_DUMP"))
				{
					char name[600];
					snprintf(name, sizeof name, "%s/fg%d.pgm", getenv("MT_PIC_DUMP"), i);
					FILE *f = fopen(name, "wb");
					if (f)
					{
						fprintf(f, "P5\n320 200\n255\n");
						for (int k = 0; k < 64000; k++) fputc(title.shown && title_fg[k] ? title_fg[k] : 0, f);
						fclose(f);
					}
					snprintf(name, sizeof name, "%s/px%d.ppm", getenv("MT_PIC_DUMP"), i);
					f = fopen(name, "wb");
					if (f)
					{
						fprintf(f, "P6\n320 200\n255\n");
						for (int k = 0; k < 64000; k++) { uint32_t c = pal[px[k]]; fputc(c >> 16, f); fputc(c >> 8, f); fputc(c, f); }
						fclose(f);
					}
				}
				/* MT_TEXT_FIND=x: report items whose text is x (where and how). */
				if (getenv("MT_TEXT_FIND"))
					for (int k = 0; k < count; k++)
						if (strcmp(items[k].text, getenv("MT_TEXT_FIND")) == 0)
							printf("find: tick %d '%s' at %d,%d %dx%d kind %d font %d hue %d level %d clip y %d-%d x %d-%d\n", i, items[k].text,
							       items[k].x, items[k].y, items[k].w, items[k].h, items[k].kind, items[k].font, items[k].hue, items[k].level,
							       items[k].clip_y0, items[k].clip_y1, items[k].clip_x0, items[k].clip_x1);
				if (getenv("MT_TEXT_FIND_DUMP") && i % 50 == 0)
					for (int k = 0; k < count; k++)
						if (strcmp(items[k].text, getenv("MT_TEXT_FIND")) == 0)
							for (int yy = items[k].y - 1; yy < items[k].y + 16 && yy < 200; yy++)
							{
								printf("  y%3d:", yy);
								for (int xx = items[k].x - 2; xx < items[k].x + 6 && xx < 320; xx++)
									printf(" %02x", px[yy * 320 + xx]);
								printf("   mask:");
								for (int xx = items[k].x - 2; xx < items[k].x + 6 && xx < 320; xx++)
									printf("%d", mask[yy * 320 + xx]);
								printf("\n");
							}
				/* Letters sorted into reading order, for MT_SCRIPT waits. */
				static int order[MT_TEXT_MAX];
				for (int k = 0; k < count; k++) order[k] = k;
				for (int a = 1; a < count; a++)
					for (int b = a; b > 0; b--)
					{
						const mt_text_item *p1 = &items[order[b - 1]], *p2 = &items[order[b]];
						if (p1->y < p2->y || (p1->y == p2->y && p1->x <= p2->x)) break;
						int t = order[b]; order[b] = order[b - 1]; order[b - 1] = t;
					}
				/* Words as the app joins them: same line, font and colour, at
				 * most 2 pixels apart; words separated by '|'. */
				size_t sl = 0;
				for (int k = 0; k < count && sl < sizeof(screen_text) - 16; k++)
				{
					const mt_text_item *it = &items[order[k]], *pv = k ? &items[order[k - 1]] : NULL;
					bool join = pv && it->kind == 0 && pv->kind == 0 && it->y == pv->y && it->font == pv->font &&
					            it->x - (pv->x + pv->w) <= 2 && it->x - (pv->x + pv->w) >= -1;
					sl += (size_t)snprintf(screen_text + sl, sizeof(screen_text) - sl, "%s%s", join || !k ? "" : "|", it->text);
				}
				screen_text[sl] = 0;
			}
			if (n != last && dump && (i % every == every - 1 || dump_now))
			{
				/* The frame as the app shows it under crisp text: masked pixels
				 * filled from known neighbours inwards, as Renderer.eraseOriginalText. */
				static uint32_t rgb[64000];
				static uint8_t known[64000], nk[64000];
				int left = 0;
				for (int k = 0; k < 64000; k++)
				{
					rgb[k] = pal[px[k]];
					known[k] = !mask[k];
					left += mask[k] != 0;
				}
				while (left > 0)
				{
					memcpy(nk, known, sizeof(nk));
					int done = 0;
					for (int k = 0; k < 64000; k++)
					{
						if (known[k]) continue;
						int x = k % 320, y = k / 320, r = 0, g = 0, b = 0, n = 0;
						for (int yy = y - 1; yy <= y + 1; yy++)
							for (int xx = x - 1; xx <= x + 1; xx++)
							{
								if (xx < 0 || yy < 0 || xx >= 320 || yy >= 200 || !known[yy * 320 + xx]) continue;
								uint32_t c = rgb[yy * 320 + xx];
								r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
							}
						if (n)
						{
							rgb[k] = (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
							nk[k] = 1;
							done++;
						}
					}
					/* Pixels filled this pass only count as known on the next one. */
					memcpy(known, nk, sizeof(known));
					if (!done) break;
					left -= done;
				}
				char path[512];
				snprintf(path, sizeof(path), "%s/t%03d.ppm", dump, i + 1);
				FILE *f = fopen(path, "wb");
				if (f)
				{
					fprintf(f, "P6 320 200 255\n");
					for (int k = 0; k < 64000; k++)
					{
						unsigned char c3[3] = { rgb[k] >> 16, rgb[k] >> 8, rgb[k] };
						fwrite(c3, 1, 3, f);
					}
					fclose(f);
				}
				snprintf(path, sizeof(path), "%s/t%03d.txt", dump, i + 1);
				f = fopen(path, "w");
				if (f)
				{
					fprintf(f, "palette");
					for (int k = 0; k < 256; k++) fprintf(f, " %06x", pal[k] & 0xffffff);
					fprintf(f, "\n");
					for (int k = 0; k < count; k++)
						fprintf(f, "%d %s %d %d %d %d cap%d hue%d lev%d dark%x black%x font%d ol%d\n", items[k].kind, items[k].text,
						        items[k].x, items[k].y, items[k].w, items[k].h, items[k].cap, items[k].hue,
						        items[k].level, items[k].outline_dark, items[k].outline_black, items[k].font, items[k].outline_level);
					fclose(f);
				}
				snprintf(path, sizeof(path), "%s/t%03d_orig.ppm", dump, i + 1);
				dump_now = 0;
				f = fopen(path, "wb");
				if (f)
				{
					fprintf(f, "P6 320 200 255\n");
					for (int k = 0; k < 64000; k++)
					{
						uint32_t c = pal[px[k]];
						unsigned char rgb[3] = { c >> 16, c >> 8, c };
						fwrite(rgb, 1, 3, f);
					}
					fclose(f);
				}
			}
			if (n != last && i % 10 == 9)
			{
				int m = 0;
				for (int k = 0; k < 64000; k++) m += mask[k];
				printf("t=%ds frame %llu: %d text items, %d masked pixels:", (i + 1) / 10, (unsigned long long)n, count, m);
				for (int k = 0; k < count && k < 40; k++)
					printf(" %s%s@%d,%d", items[k].kind ? "[" : "", items[k].text, items[k].x, items[k].y);
				printf("\n");
			}
			last = n;
		}
		else
			last = mt_copy_frame(last, px, pal);
		if (getenv("MT_FPS_LOG") && i % 10 == 9)
			printf("t=%ds frames=%llu\n", (i + 1) / 10, (unsigned long long)last);
	}
	if (getenv("MT_QUIT"))
	{
		/* As the Mac app does on Command-Q: the game saves, then exits the process. */
		mt_push_quit();
		sleep(5);
		printf("game did not quit\n");
	}
	running = 0;
	if (wav)
		pthread_join(at, NULL);
	printf("frames presented: %llu\n", (unsigned long long)last);
	return 0;
}
