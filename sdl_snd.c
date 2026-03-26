#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"
#include "digger_math.h"
#include "digger_log.h"
#include "newsnd.h"
#include "sdl_snd.h"

static void fill_audio(void *udata, uint8_t *stream, int len);

_Atomic bool wave_device_available = false;

bool initsounddevice(void)
{
	return(true);
}

struct sudata {
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID dev;
    int16_t *buf;
    unsigned int bsize;
    struct bqd_filter *lp_fltr;
    struct bqd_filter *hp_fltr;
};

static struct sudata *sud;

bool setsounddevice(uint16_t samprate, uint16_t bufsize)
{
	SDL_AudioSpec wanted;
	bool result = false;

	assert(sud == NULL);
        sud = (struct sudata*)malloc(sizeof(*sud));
        if (sud == NULL) {
                digger_log_printf("setsounddevice: malloc(3) failed\n");

                return (false);
        }
        wave_device_available = false;
        memset(sud, '\0', sizeof(*sud));
        SDL_zero(wanted);
        SDL_zero(sud->obtained);
	wanted.freq = samprate;
	wanted.samples = bufsize;
	wanted.channels = 1;
	wanted.format = AUDIO_S16;
	wanted.userdata = sud;
	wanted.callback = fill_audio;

	if ((SDL_Init(SDL_INIT_AUDIO)) >= 0) {
		sud->dev = SDL_OpenAudioDevice(NULL, 0, &wanted, &sud->obtained, 0);
		if (sud->dev > 0)
			result = true;
	}
	if (result == false) {
		digger_log_printf("Couldn't open audio: %s\n", SDL_GetError());
                free(sud);
                sud = NULL;
                return (false);
        }
#if defined(DIGGER_DEBUG)
	digger_log_printf("setsounddevice: wanted.samples=%d obtained.samples=%d\n",
	    wanted.samples, sud->obtained.samples);
#endif
        sud->bsize = sud->obtained.size;
	sud->buf = (int16_t*)malloc(sud->bsize);
        if (sud->buf == NULL) {
                digger_log_printf("setsounddevice: malloc(3) failed\n");
                SDL_CloseAudioDevice(sud->dev);
                free(sud);
                sud = NULL;
                return (false);
        }
        sud->lp_fltr = bqd_lp_init(sud->obtained.freq, 4000);
        sud->hp_fltr = bqd_hp_init(sud->obtained.freq, 1000);
	SDL_PauseAudioDevice(sud->dev, 0);

	return(result);
}

static void fill_audio(void *udata, uint8_t *stream, int len)
{
	int i;
        struct sudata *sud;
#if !defined(NO_SND_FILTER)
        double sample;
#endif

        if (!wave_device_available)
		wave_device_available = true;
	sud = (struct sudata *)udata;
        SDL_memset(stream, sud->obtained.silence, len);
	if (len > sud->bsize) {
                digger_log_printf("fill_audio: OUCH, len > bsize!\n");
		len = sud->bsize;
        }
	for (i = 0; i < len / sizeof(int16_t); i++) {
#if !defined(NO_SND_FILTER)
		sample = getsample();
		sample = bqd_apply(sud->hp_fltr, (sample - 127.0) * 128.0);
		double out = round(bqd_apply(sud->lp_fltr, sample));
		if (out > INT16_MAX)
			out = INT16_MAX;
		else if (out < INT16_MIN)
			out = INT16_MIN;
		sud->buf[i] = (int16_t)out;
#else
		sud->buf[i] = getsample();
#endif
        }

	SDL_MixAudioFormat(stream, (uint8_t *)sud->buf, sud->obtained.format, len,
            SDL_MIX_MAXVOLUME);
}


static bool wave_device_paused = false;

void pausesounddevice(bool p)
{

	if (wave_device_paused == p)
		return;
	SDL_PauseAudioDevice(sud->dev, p ? 1 : 0);
	wave_device_paused = p;
}

void wakesounddevice(void)
{
	SDL_AudioStatus st;

	if (sud == NULL || sud->dev == 0)
		return;
	st = SDL_GetAudioDeviceStatus(sud->dev);
	if (st == SDL_AUDIO_PLAYING)
		return;
	wave_device_available = false;
	SDL_PauseAudioDevice(sud->dev, 0);
	wave_device_paused = false;
#if defined(DIGGER_DEBUG)
	digger_log_printf("wakesounddevice: status=%d -> PLAYING\n", (int)st);
#endif
}
