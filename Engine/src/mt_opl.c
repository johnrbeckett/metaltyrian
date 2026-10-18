/*
 * MetalTyrian: the music's AdLib chip, emulated by Nuked OPL3 (opl3.c, by
 * Nuke.YKT, LGPL 2.1 or later) in place of OpenTyrian's DOSBox emulator.
 * The game's audio runs at the chip's own rate (MT_AUDIO_RATE, 49716 Hz), so
 * the chip needs no resampling here; macOS converts the finished mix to the
 * output device's rate. Not part of OpenTyrian.
 */
#ifdef METALTYRIAN

#include "opl.h"
#include "opl3.h"

#include "metaltyrian_host.h"

static opl3_chip chip;

void mt_opl_init(void)
{
	OPL3_Reset(&chip, MT_AUDIO_RATE);
}

void mt_opl_write(Bitu idx, Bit8u val)
{
	OPL3_WriteRegBuffered(&chip, (uint16_t)idx, val);
}

/* The game's tunes are OPL2 (mono): both chip outputs carry the same signal. */
void mt_opl_update(Bit16s *buf, Bits num)
{
	int16_t frame[2];
	for (Bits i = 0; i < num; i++)
	{
		OPL3_GenerateResampled(&chip, frame);
		buf[i] = frame[0];
	}
}

#endif /* METALTYRIAN */
