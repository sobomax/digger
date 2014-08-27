#include <stdlib.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"

void fill_audio(void *udata, uint8_t *stream, int len);
uint8_t getsample(void);

bool wave_device_available = false;

bool initsounddevice(void)
{
	return(true);
}

struct sudata {
    SDL_AudioSpec obtained;
    uint8_t *buf;
    uint16_t bsize;
};

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
	for(i = 0; i<len; i++)
		sud->buf[i] = getsample();

	SDL_MixAudioFormat(stream, sud->buf, sud->obtained.format, len,
            SDL_MIX_MAXVOLUME / 2);
}


void killsounddevice(void)
{
	SDL_PauseAudio(1);
}

