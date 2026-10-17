/* Headless Linux host for testing the engine on the SDL shim. */
#include "metaltyrian_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
	const char *gargv[] = { "metaltyrian", "-t", argv[1] };
	mt_start_game(3, gargv);
	int seconds = argc > 2 ? atoi(argv[2]) : 100;
	static uint8_t px[320 * 200]; static uint32_t pal[256];
	uint64_t last = 0;
	for (int i = 0; i < seconds * 10 && !mt_game_finished(); ++i)
	{
		usleep(100000);
		last = mt_copy_frame(last, px, pal);
	}
	printf("frames presented: %llu\n", (unsigned long long)last);
	return 0;
}
