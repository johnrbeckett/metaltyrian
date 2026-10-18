/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "loudness.h"

#include "file.h"
#include "lds_play.h"
#include "nortsong.h"
#include "opentyr.h"
#include "params.h"

#ifdef METALTYRIAN
#include "metaltyrian_host.h"
#include <math.h>
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_QUALITY 4  // 44.1 kHz

int audioSampleRate = 0;

bool music_stopped = true;
unsigned int song_playing = 0;

bool audio_disabled = false, music_disabled = false, samples_disabled = false;

static SDL_AudioDeviceID audioDevice = 0;

static Uint8 musicVolume = 255;
static Uint8 sampleVolume = 255;

static const float volumeRange = 30.0f;  // dB

// Fixed point Q20.12; needs to be able to store (10 * INT16_MIN/MAX)
static Sint32 volumeFactorTable[256];
#define TO_FIXED(x) ((Sint32)((x) * (1 << 12)))
#define FIXED_TO_INT(x) ((Sint32)((x) >> 12))

// Twice the Loudness update rate (in updates/second).  In Tyrian, Loudness
// updates were performed at the same rate as the game timer, which varied
// depending on the game speed (~69.57 Hz at most game speeds).  We don't have
// the same limitations, so we'll keep the update rate constant, but we do want
// to stick to integer math, so we'll update at 69.5 Hz.
static const int ldsUpdate2Rate = 139;  // 69.5 * 2

static int samplesPerLdsUpdate;
static int samplesPerLdsUpdateFrac;

static int samplesUntilLdsUpdate = 0;
static int samplesUntilLdsUpdateFrac = 0;

static FILE *music_file = NULL;
static Uint32 *song_offset;
static Uint16 song_count = 0;

#define CHANNEL_COUNT 8
static const Sint16 *channelSamples[CHANNEL_COUNT];
static size_t channelSampleCount[CHANNEL_COUNT] = { 0 };
static Uint8 channelVolume[CHANNEL_COUNT];
#define CHANNEL_VOLUME_LEVELS 8

static void audioCallback(void *userdata, Uint8 *stream, int size);

static void load_song(unsigned int song_num);

bool init_audio(void)
{
	if (audio_disabled)
		return false;

	SDL_AudioSpec ask, got;

	ask.freq = 11025 * OUTPUT_QUALITY;
	ask.format = AUDIO_S16SYS;
#ifdef METALTYRIAN
	ask.channels = 2;  // effects panned by where they happen (see audioCallback)
#else
	ask.channels = 1;
#endif
	ask.samples = 256 * OUTPUT_QUALITY; // ~23 ms
	ask.callback = audioCallback;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		fprintf(stderr, "error: failed to initialize SDL audio: %s\n", SDL_GetError());
		audio_disabled = true;
		return false;
	}

	int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
#if SDL_VERSION_ATLEAST(2, 0, 9)
	allowedChanges |= SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
#endif
	audioDevice = SDL_OpenAudioDevice(/*device*/ NULL, /*iscapture*/ 0, &ask, &got, allowedChanges);

	if (audioDevice == 0)
	{
		fprintf(stderr, "error: SDL failed to open audio device: %s\n", SDL_GetError());
		audio_disabled = true;
		return false;
	}

	audioSampleRate = got.freq;

	samplesPerLdsUpdate = 2 * (audioSampleRate / ldsUpdate2Rate);
	samplesPerLdsUpdateFrac = 2 * (audioSampleRate % ldsUpdate2Rate);

	volumeFactorTable[0] = 0;
	for (size_t i = 1; i < 256; ++i)
		volumeFactorTable[i] = TO_FIXED(powf(10, (255 - i) * (-volumeRange / (20.0f * 255))));

	opl_init();

	SDL_PauseAudioDevice(audioDevice, 0); // unpause

	return true;
}

#ifdef METALTYRIAN
/* MetalTyrian's mixer: stereo, with each effect placed left or right by
 * where it happens on screen (moderately: 70/30 at the screen's edges), the
 * music in the middle, and a gentle limiter instead of hard clipping when
 * many sounds pile up. */

static float channelGainL[CHANNEL_COUNT], channelGainR[CHANNEL_COUNT];
static float pendingPan = 0.0f;
static float limiterGain = 1.0f;

/* Music level against the effects: Nuked OPL3 is about as loud as the
 * DOSBox emulator the game's mix was set for (twice its raw level). */
#define MT_MUSIC_BOOST 2

void mt_sound_pan_next(float pan)
{
	pendingPan = pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
}

static void mt_mix(Sint16 *samples, int frames)
{
	static Sint16 music[1024];
	const float musicFactor = volumeFactorTable[musicVolume] * MT_MUSIC_BOOST / 4096.0f;
	float sampleFactors[CHANNEL_VOLUME_LEVELS];
	for (int i = 0; i < CHANNEL_VOLUME_LEVELS; ++i)
		sampleFactors[i] = volumeFactorTable[sampleVolume] * (i + 1) / (float)CHANNEL_VOLUME_LEVELS / 4096.0f;
	const bool playMusic = !music_disabled && !music_stopped;
	const bool playSamples = !samples_disabled;
	const float threshold = 29200.0f;                 // -1 dB
	const float release = 1.0f / (0.12f * MT_AUDIO_RATE);   // back to full in about 120 ms

	while (frames > 0)
	{
		const int chunk = frames < 1024 ? frames : 1024;
		if (playMusic)
		{
			Sint16 *remaining = music;
			int remainingCount = chunk;
			while (remainingCount > 0)
			{
				if (samplesUntilLdsUpdate == 0)
				{
					lds_update();
					samplesUntilLdsUpdate += samplesPerLdsUpdate;
					samplesUntilLdsUpdateFrac += samplesPerLdsUpdateFrac;
					if (samplesUntilLdsUpdateFrac >= ldsUpdate2Rate)
					{
						samplesUntilLdsUpdate += 1;
						samplesUntilLdsUpdateFrac -= ldsUpdate2Rate;
					}
				}
				int count = MIN(samplesUntilLdsUpdate, remainingCount);
				opl_update(remaining, count);
				remaining += count;
				remainingCount -= count;
				samplesUntilLdsUpdate -= count;
			}
		}

		for (int f = 0; f < chunk; ++f)
		{
			float m = playMusic ? music[f] * musicFactor : 0.0f;
			float l = m, r = m;
			if (playSamples)
			{
				for (size_t i = 0; i < CHANNEL_COUNT; ++i)
				{
					if (channelSampleCount[i] > 0)
					{
						const float v = *channelSamples[i] * sampleFactors[channelVolume[i]];
						l += v * channelGainL[i];
						r += v * channelGainR[i];
						channelSamples[i] += 1;
						channelSampleCount[i] -= 1;
					}
				}
			}
			/* Limiter: turn down at once when a peak would clip, back up slowly. */
			const float peak = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
			const float target = peak > threshold ? threshold / peak : 1.0f;
			if (target < limiterGain)
				limiterGain = target;
			else
				limiterGain += (target - limiterGain) * release;
			l *= limiterGain;
			r *= limiterGain;
			samples[2 * f]     = (Sint16)(l < -32768.0f ? -32768 : l > 32767.0f ? 32767 : l);
			samples[2 * f + 1] = (Sint16)(r < -32768.0f ? -32768 : r > 32767.0f ? 32767 : r);
		}
		samples += 2 * chunk;
		frames -= chunk;
	}
}
#endif

static void audioCallback(void *userdata, Uint8 *stream, int size)
{
#ifdef METALTYRIAN
	(void)userdata;
	mt_mix((Sint16 *)stream, size / (int)(2 * sizeof(Sint16)));
	return;
#endif
	(void)userdata;

	Sint16 *const samples = (Sint16 *)stream;
	const int samplesCount = size / sizeof (Sint16);

	if (!music_disabled && !music_stopped)
	{
		Sint16 *remaining = samples;
		int remainingCount = samplesCount;
		while (remainingCount > 0)
		{
			if (samplesUntilLdsUpdate == 0)
			{
				lds_update();

				// The number of samples that should be produced per Loudness
				// update is not an integer, but we can only produce an integer
				// number of samples, so we accumulate the fractional samples
				// until it amounts to a whole sample.
				samplesUntilLdsUpdate += samplesPerLdsUpdate;
				samplesUntilLdsUpdateFrac += samplesPerLdsUpdateFrac;
				if (samplesUntilLdsUpdateFrac >= ldsUpdate2Rate)
				{
					samplesUntilLdsUpdate += 1;
					samplesUntilLdsUpdateFrac -= ldsUpdate2Rate;
				}
			}

			int count = MIN(samplesUntilLdsUpdate, remainingCount);

			opl_update(remaining, count);

			remaining += count;
			remainingCount -= count;

			samplesUntilLdsUpdate -= count;
		}
	}
	else
	{
		for (int i = 0; i < samplesCount; ++i)
			samples[i] = 0;
	}

	Sint32 musicVolumeFactor = volumeFactorTable[musicVolume];
	musicVolumeFactor *= 2;  // OPL emulator is too quiet

	if (samples_disabled && !music_disabled)
	{
		// Mix music
		Sint16 *remaining = samples;
		int remainingCount = samplesCount;
		while (remainingCount > 0)
		{
			Sint32 sample = *remaining * musicVolumeFactor;

			sample = FIXED_TO_INT(sample);
			*remaining = MIN(MAX(INT16_MIN, sample), INT16_MAX);

			remaining += 1;
			remainingCount -= 1;
		}
	}
	else if (!samples_disabled)
	{
		Sint32 sampleVolumeFactor = volumeFactorTable[sampleVolume];
		Sint32 sampleVolumeFactors[CHANNEL_VOLUME_LEVELS];
		for (int i = 0; i < CHANNEL_VOLUME_LEVELS; ++i)
			sampleVolumeFactors[i] = sampleVolumeFactor * (i + 1) / CHANNEL_VOLUME_LEVELS;

		// Mix music and channels
		Sint16 *remaining = samples;
		int remainingCount = samplesCount;
		while (remainingCount > 0)
		{
			Sint32 sample = *remaining * musicVolumeFactor;

			for (size_t i = 0; i < CHANNEL_COUNT; ++i)
			{
				if (channelSampleCount[i] > 0)
				{
					sample += *channelSamples[i] * sampleVolumeFactors[channelVolume[i]];

					channelSamples[i] += 1;
					channelSampleCount[i] -= 1;
				}
			}

			sample = FIXED_TO_INT(sample);
			*remaining = MIN(MAX(INT16_MIN, sample), INT16_MAX);

			remaining += 1;
			remainingCount -= 1;
		}
	}
}

void deinit_audio(void)
{
	if (audio_disabled)
		return;

	if (audioDevice != 0)
	{
		SDL_PauseAudioDevice(audioDevice, 1); // pause
		SDL_CloseAudioDevice(audioDevice);
		audioDevice = 0;
	}

	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	memset(channelSampleCount, 0, sizeof channelSampleCount);

	lds_free();
}

void load_music(void)  // FKA NortSong.loadSong
{
	if (music_file == NULL)
	{
		music_file = dir_fopen_die(data_dir(), "music.mus", "rb");

		fread_u16_die(&song_count, 1, music_file);

		song_offset = malloc((song_count + 1) * sizeof(*song_offset));

		fread_u32_die(song_offset, song_count, music_file);

		song_offset[song_count] = ftell_eof(music_file);
	}
}

static void load_song(unsigned int song_num)  // FKA NortSong.loadSong
{
	if (song_num < song_count)
	{
		unsigned int song_size = song_offset[song_num + 1] - song_offset[song_num];
		lds_load(music_file, song_offset[song_num], song_size);
	}
	else
	{
		fprintf(stderr, "warning: failed to load song %d\n", song_num + 1);
	}
}

void play_song(unsigned int song_num)  // FKA NortSong.playSong
{
	if (audio_disabled)
		return;

	if (song_num != song_playing)
	{
		SDL_LockAudioDevice(audioDevice);

		music_stopped = true;

		SDL_UnlockAudioDevice(audioDevice);

		load_song(song_num);

		song_playing = song_num;
	}

	SDL_LockAudioDevice(audioDevice);

	music_stopped = false;

	SDL_UnlockAudioDevice(audioDevice);
}

void restart_song(void)  // FKA Player.selectSong(1)
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	lds_rewind();

	music_stopped = false;

	SDL_UnlockAudioDevice(audioDevice);
}

void stop_song(void)  // FKA Player.selectSong(0)
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	music_stopped = true;

	SDL_UnlockAudioDevice(audioDevice);
}

void fade_song(void)  // FKA Player.selectSong($C001)
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	lds_fade(1);

	SDL_UnlockAudioDevice(audioDevice);
}

void set_volume(Uint8 musicVolume_, Uint8 sampleVolume_)  // FKA NortSong.setVol and Player.setVol
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	musicVolume = musicVolume_;
	sampleVolume = sampleVolume_;

	SDL_UnlockAudioDevice(audioDevice);
}

void multiSamplePlay(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol)  // FKA Player.multiSamplePlay
{
	assert(chan < CHANNEL_COUNT);
	assert(vol < CHANNEL_VOLUME_LEVELS);

	if (audio_disabled || samples_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	channelSamples[chan] = samples;
	channelSampleCount[chan] = sampleCount;
	channelVolume[chan] = vol;
#ifdef METALTYRIAN
	/* Pan law: 70/30 at the edges, both sides full in the middle. */
	const float g = mt_setting(MT_SETTING_STEREO) == 0 ? 0.4f * pendingPan : 0.0f;
	const float norm = 1.0f + fabsf(g);
	channelGainL[chan] = (1.0f - g) / norm;
	channelGainR[chan] = (1.0f + g) / norm;
	pendingPan = 0.0f;
#endif

	SDL_UnlockAudioDevice(audioDevice);
}
