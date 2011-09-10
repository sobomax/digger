#include <stdlib.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"

void fill_audio(void *udata, uint8_t *stream, int len);
uint8_t getsample(void);

uint8_t *buf;
uint16_t bsize;

bool wave_device_available = false;

bool initsounddevice(void)
{
	return(true);
}

bool setsounddevice(int base, int irq, int dma, uint16_t samprate, uint16_t bufsize)
{
	SDL_AudioSpec wanted;
	bool result = false;
	
	wanted.freq = samprate;
	wanted.samples = bufsize;
	wanted.channels = 1;
	wanted.format = AUDIO_U8;
	wanted.userdata = NULL;
	wanted.callback = fill_audio;

#ifdef _VGL
	restorekeyb();
#endif

	if ((SDL_Init(SDL_INIT_AUDIO)) >= 0)
		if ((SDL_OpenAudio(&wanted, NULL)) >= 0)
			result = true;
	if (result == false)
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
	else {
		buf = malloc(bufsize);
		bsize = bufsize;
		wave_device_available = true;
	}

#ifdef _VGL
	initkeyb();
#endif

	return(result);
}

void fill_audio(void *udata, uint8_t *stream, int len)
{
	int i;

	if (len > bsize)
		len = bsize;
	for(i = 0; i<len; i++)
		buf[i] = getsample();

	SDL_MixAudio(stream, buf, len, SDL_MIX_MAXVOLUME);
}


void killsounddevice(void)
{
	SDL_PauseAudio(1);
}

