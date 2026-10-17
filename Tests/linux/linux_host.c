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

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void *audio_thread(void *arg)
{
	const char *path = arg;
	FILE *f = fopen(path, "wb");
	if (!f) { perror(path); return NULL; }
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, f);
	put32(f, 16); put16(f, 1); put16(f, 1); put32(f, MT_AUDIO_RATE); put32(f, MT_AUDIO_RATE * 2);
	put16(f, 2); put16(f, 16);
	fwrite("data\0\0\0\0", 1, 8, f);
	uint32_t bytes = 0;
	enum { CHUNK = 441 };  /* 10 ms, like a small Core Audio buffer */
	float buf[CHUNK];
	int16_t pcm[CHUNK];
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (running)
	{
		mt_audio_render(buf, CHUNK);
		for (int i = 0; i < CHUNK; i++)
		{
			float v = buf[i] * 32768.0f;
			pcm[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
		}
		fwrite(pcm, 2, CHUNK, f);
		bytes += CHUNK * 2;
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
		last = mt_copy_frame(last, px, pal);
		if (getenv("MT_FPS_LOG") && i % 10 == 9)
			printf("t=%ds frames=%llu\n", (i + 1) / 10, (unsigned long long)last);
	}
	running = 0;
	if (wav)
		pthread_join(at, NULL);
	printf("frames presented: %llu\n", (unsigned long long)last);
	return 0;
}
