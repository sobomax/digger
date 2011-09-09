#include <stdlib.h>
#include <SDL.h>

#include "def.h"
#include "device.h"
#include "hardware.h"

void fill_audio(void *udata, Uint8 *stream, int len);
samp getsample(void);

samp *buf;
Uint4 bsize;

bool wave_device_available = FALSE;

bool initsounddevice(void)
{
	return(TRUE);
}

bool setsounddevice(int base, int irq, int dma, Uint4 samprate, Uint4 bufsize)
{
	SDL_AudioSpec wanted;
	bool result = FALSE;
	
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
			result = TRUE;
	if (result == FALSE)
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
	else {
		buf = malloc(bufsize);
		bsize = bufsize;
		wave_device_available = TRUE;
	}

#ifdef _VGL
	initkeyb();
#endif

	return(result);
}

void fill_audio(void *udata, Uint8 *stream, int len)
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

