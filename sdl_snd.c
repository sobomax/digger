#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"
#include "digger_math.h"

void fill_audio(void *udata, uint8_t *stream, int len);
uint8_t getsample(void);

bool wave_device_available = false;

bool initsounddevice(void)
{
	return(true);
}

struct fo_filter {
    double a;
    double b;
    double z0;
    double z1;
};

struct sudata {
    SDL_AudioSpec obtained;
    uint8_t *buf;
    uint16_t bsize;
    struct fo_filter *fltr;
};

static struct fo_filter *
fo_init(double Fs, double Fc)
{
        struct fo_filter *fofp;
        double n, w;

        fofp = malloc(sizeof(*fofp));
        memset(fofp, '\0', sizeof(*fofp));
        if (Fs < Fc * 2.0) {
                fprintf(stderr, "fo_init: cutoff frequency (%f) should be less "
                    "than half of the sampling rate (%f)\n", Fc, Fs);
                abort();
        }
        w = tan(D_PI * Fc / Fs);
        n = 1.0 / (1.0 + w);
        fofp->a = n * (w - 1);
        fofp->b = n * w;
        return (fofp);
}

static double
fo_apply(struct fo_filter *fofp, double x)
{
        fofp->z1 = (x * fofp->b) + (fofp->z0 * fofp->b) - (fofp->z1 * fofp->a);
        fofp->z0 = x;
        return (fofp->z1);
}

bool setsounddevice(int base, int irq, int dma, uint16_t samprate, uint16_t bufsize)
{
	SDL_AudioSpec wanted;
        struct sudata *sud;
	bool result = false;
	
        sud = malloc(sizeof(*sud));
        if (sud == NULL) {
                fprintf(stderr, "setsounddevice: malloc(3) failed\n");
                return (false);
        }
        memset(sud, '\0', sizeof(*sud));
        SDL_zero(wanted);
        SDL_zero(sud->obtained);
	wanted.freq = samprate;
	wanted.samples = bufsize;
	wanted.channels = 1;
	wanted.format = AUDIO_U8;
	wanted.userdata = sud;
	wanted.callback = fill_audio;

#ifdef _VGL
	restorekeyb();
#endif

	if ((SDL_Init(SDL_INIT_AUDIO)) >= 0)
		if ((SDL_OpenAudio(&wanted, &sud->obtained)) >= 0)
			result = true;
	if (result == false) {
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
                free(sud);
                return (false);
        }
        sud->bsize = sud->obtained.size;
	sud->buf = malloc(sud->bsize);
        if (sud->buf == NULL) {
                fprintf(stderr, "setsounddevice: malloc(3) failed\n");
                SDL_CloseAudio();
                free(sud);
                return (false);
        }
        sud->fltr = fo_init(sud->obtained.freq, 4000);
        sud->fltr->z0 = sud->obtained.silence;
        sud->fltr->z1 = sud->obtained.silence;
	wave_device_available = true;

#ifdef _VGL
	initkeyb();
#endif

	return(result);
}

void fill_audio(void *udata, uint8_t *stream, int len)
{
	int i;
        struct sudata *sud;

        sud = (struct sudata *)udata;
        SDL_memset(stream, sud->obtained.silence, len);
	if (len > sud->bsize) {
                fprintf(stderr, "fill_audio: OUCH, len > bsize!\n");
		len = sud->bsize;
        }
	for (i = 0; i < len; i++) {
		sud->buf[i] = fo_apply(sud->fltr, getsample());
        }

	SDL_MixAudioFormat(stream, sud->buf, sud->obtained.format, len,
            SDL_MIX_MAXVOLUME / 2);
}


void killsounddevice(void)
{
	SDL_PauseAudio(1);
}

