/*
 * ---------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42, (c) Poul-Henning Kamp): Maxim
 * Sobolev <sobomax@altavista.net> wrote this file. As long as you retain
 * this  notice you can  do whatever you  want with this stuff. If we meet
 * some day, and you think this stuff is worth it, you can buy me a beer in
 * return.
 * 
 * Maxim Sobolev
 * --------------------------------------------------------------------------- 
 */

/* malloc() and friends */
#include <stdlib.h>
/* Lovely SDL */
#include <SDL.h>
#include <SDL_syswm.h>

#include "def.h"
#include "hardware.h"
#include "title_gz.h"
#include "icon.h"
#include "sdl_vid.h"

extern uint8_t *vgatable[];
extern uint8_t *ascii2vga[];

static uint8_t **sprites = vgatable;
static uint8_t **alphas = ascii2vga;

static int16_t xratio = 2;
static int16_t yratio = 2;
static int16_t yoffset = 0;
static int16_t hratio = 2;
static int16_t wratio = 2;
#define virt2scrx(x) (x*xratio)
#define virt2scry(y) (y*yratio+yoffset)
#define virt2scrw(w) (w*wratio)
#define virt2scrh(h) (h*hratio)

/* palette1, normal intensity */
static SDL_Color vga16_pal1[] = \
{{0,0,0,0},{0,0,128,0},{0,128,0,0},{0,128,128,0},{128,0,0,0},{128,0,128,0} \
,{128,64,0,0},{128,128,128,0},{64,64,64,0},{0,0,255,0},{0,255,0,0} \
,{0,255,255,0},{255,0,0,0},{255,0,255,0},{255,255,0,0},{255,255,255,0}};
/* palette1, high intensity */
static SDL_Color vga16_pal1i[] = \
{{0,0,0,0},{0,0,255,0},{0,255,0,0},{0,255,255,0},{255,0,0,0},{255,0,255,0} \
,{255,128,0,0},{196,196,196,0},{128,128,128,0},{128,128,255,0},{128,255,128,0} \
,{128,255,255,0},{255,128,128,0},{255,128,255,0},{255,255,128,0},{255,255,255,0}};
/* palette2, normal intensity */
static SDL_Color vga16_pal2[] = \
{{0,0,0,0},{0,128,0,0},{128,0,0,0},{128,64,0,0},{0,0,128,0},{0,128,128,0} \
,{128,0,128,0},{128,128,128,0},{64,64,64,0},{0,255,0,0},{255,0,0,0} \
,{255,255,0,0},{0,0,255,0},{0,255,255,0},{255,0,255,0},{255,255,255,0}};
/* palette2, high intensity */
static SDL_Color vga16_pal2i[] = \
{{0,0,0,0},{0,255,0,0},{255,0,0,0},{255,128,0,0},{0,0,255,0},{0,255,255,0} \
,{255,0,255,0},{196,196,196,0},{128,128,128,0},{128,255,128,0},{255,128,128,0} \
,{255,255,128,0},{128,128,255,0},{128,255,255,0},{255,128,255,0},{255,255,255,0}};

static SDL_Color *npalettes[] = {vga16_pal1, vga16_pal2};
static SDL_Color *ipalettes[] = {vga16_pal1i, vga16_pal2i};
static int16_t	currpal=0;

#ifdef UNIX
static Window x11_parent = 0;
#endif
#if defined(SDL_OLD)
static uint32_t	addflag=0;
#endif

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *roottxt = NULL;
static SDL_Palette *rootpal = NULL;
static SDL_Surface *screen = NULL;
static SDL_Surface *screen16 = NULL;

SDL_Surface *ch2bmap(uint8_t *sprite, int16_t w, int16_t h)
{
	int16_t realw, realh;
	SDL_Surface *tmp;

	realw = virt2scrw(w*4);
	realh = virt2scrh(h);
	tmp = SDL_CreateRGBSurfaceFrom(sprite, realw, realh, 8, realw, 0, 0, 0, 0);
	SDL_SetPaletteColors(tmp->format->palette, rootpal->colors, 0, rootpal->ncolors);
	
	return(tmp);
}

void graphicsoff(void)
{
}

#ifdef UNIX
void
x11_set_parent(Window parent)
{
#if 0
        SDL_SysWMinfo sdlInfo;

        SDL_VERSION(&sdlInfo.version);
        SDL_GetWMInfo(&sdlInfo);
        XReparentWindow(sdlInfo.info.x11.gfxdisplay, sdlInfo.info.x11.window, parent, 0, 0);
        XDestroyWindow(sdlInfo.info.x11.gfxdisplay, sdlInfo.info.x11.wmwindow);
#endif
}
#endif

bool setmode(void)
{
#if defined(SDL_OLD)
#ifdef UNIX
        static int x11_parent_inited = 0;

        if (x11_parent && x11_parent_inited == 0) {
                addflag |= SDL_NOFRAME;
        }
#endif
        screen = SDL_SetVideoMode(640, 400, 8, SDL_HWSURFACE | SDL_HWPALETTE | \
          SDL_DOUBLEBUF | addflag);
        if (screen == NULL)
		return(false);
#ifdef UNIX
        if (x11_parent && x11_parent_inited == 0) {
                x11_set_parent(x11_parent);
        }
#endif
#endif
	return(true);
}

void switchmode(void)
{
#if defined(SDL_OLD)
	uint32_t saved;
	SDL_Surface *tmp = NULL;
	SDL_Surface *oldscreen;
	
	vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
	oldscreen = screen;
	saved = addflag;

	if(addflag == 0)
		addflag = SDL_FULLSCREEN;
	else
		addflag = 0;
	if(setmode() == false) {
		addflag = saved;
		if(setmode() == false) {
			fprintf(stderr, "Fatal: failed to change videomode and"\
				"fallback mode failed as well. Exitting.\n");
			exit(1);
		}
	}

	SDL_SetPaletteColors(screen->format->palette, rootpal->colors, 0, \
	    rootpal->ncolors);
	vgaputi(0, 0, (uint8_t *)&tmp, 80, 200);
	SDL_FreeSurface(tmp);
	SDL_FreeSurface(oldscreen);
#endif
}


void vgainit(void)
{
	SDL_Surface *tmp = NULL;
	
	tmp = SDL_CreateRGBSurfaceFrom(Icon, 64, 64, 8, 64, 0, 0, 0, 0);
	SDL_SetColorKey(tmp, SDL_TRUE, 247); 			

	tmp->format->palette->colors = IconPalette;
	
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

        window = SDL_CreateWindow("D I G G E R", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, 640, 400, SDL_WINDOW_OPENGL);
        if (window == NULL) {
                fprintf(stderr, "SDL_CreateWindow() failed\n");
                exit(1);
        }
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (renderer == NULL) {
                fprintf(stderr, "SDL_CreateRenderer() failed\n");
                exit(1);
        }
        roottxt = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
             SDL_TEXTUREACCESS_STREAMING, 640, 400);
        if (roottxt == NULL) {
                fprintf(stderr, "SDL_CreateTexture() failed: %s\n", SDL_GetError());
                exit(1);
        }
        rootpal = SDL_AllocPalette(16);
        if (rootpal == NULL) {
                fprintf(stderr, "SDL_AllocPalette() failed: %s\n", SDL_GetError());
                exit(1);
        }
        screen = SDL_CreateRGBSurface(0, 640, 400, 32,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (screen == NULL) {
                fprintf(stderr, "SDL_CreateRGBSurface() failed\n");
                exit(1);
        }

#if defined(SDL_OLD)
	SDL_WM_SetIcon(tmp, NULL);
#endif
	if(setmode() == false) {
		fprintf(stderr, "Couldn't set 640x400x8 video mode: %s\n", SDL_GetError());
		exit(1);
        }
	SDL_ShowCursor(0);
}

void vgaclear(void)
{
	SDL_Surface *tmp = NULL;
	
	vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
	memset(tmp->pixels, 0x00, tmp->w*tmp->h);
	vgaputi(0, 0, (uint8_t *)&tmp, 80, 200);
	SDL_FreeSurface(tmp);
}
void setpal(SDL_Color *pal)
{

	SDL_SetPaletteColors(rootpal, pal, 0, 16);
}
	
void vgainten(int16_t inten)
{
	if(inten == 1)
		setpal(ipalettes[currpal]);
	else
		setpal(npalettes[currpal]);
}

void vgapal(int16_t pal)
{
	setpal(npalettes[pal]);
	currpal = pal;
}

void doscreenupdate(void)
{

        SDL_UpdateTexture(roottxt, NULL, screen->pixels, screen->pitch);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, roottxt, NULL, NULL);
        SDL_RenderPresent(renderer);
}

void vgaputi(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h)
{
	SDL_Surface *tmp;
	SDL_Palette *reserv;
        SDL_Rect rect;

	rect.x = virt2scrx(x);
	rect.y = virt2scry(y);
	rect.w = virt2scrw(w*4);
	rect.h = virt2scrh(h);

	memcpy(&tmp, p, (sizeof (SDL_Surface *)));
	reserv = tmp->format->palette;
	tmp->format->palette = rootpal;
	SDL_BlitSurface(tmp, NULL, screen, &rect);
	tmp->format->palette = reserv;
}

void vgageti(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h)
{
	SDL_Surface *tmp;
	SDL_Rect src;
	
	memcpy(&tmp, p, (sizeof (SDL_Surface *)));
	if (tmp != NULL)
		SDL_FreeSurface(tmp); /* Destroy previously allocated bitmap */

	src.x = virt2scrx(x);
	src.y = virt2scry(y);
	src.w = virt2scrw(w*4);
	src.h = virt2scrh(h);

	tmp = SDL_CreateRGBSurface(0, src.w, src.h, 8, 0, 0, 0, 0);
	SDL_SetPaletteColors(tmp->format->palette, rootpal->colors, 0, rootpal->ncolors);
	SDL_BlitSurface(screen, &src, tmp, NULL);
	memcpy(p, &tmp, (sizeof (SDL_Surface *)));
}

int16_t vgagetpix(int16_t x, int16_t y)
{	
	SDL_Surface *tmp = NULL;
	uint16_t xi,yi;
	uint16_t i = 0;
	int16_t rval = 0;
	uint8_t *pixels;

	if ((x > 319) || (y > 199))
	       return (0xff);

	vgageti(x, y, (uint8_t *)&tmp, 1, 1);
	pixels = (uint8_t *)tmp->pixels;
	for (yi=0;yi<tmp->h;yi++)
		for (xi=0;xi<tmp->w;xi++)
			if (pixels[i++])
				rval |= 0x80 >> xi;

	SDL_FreeSurface(tmp);

	return(rval & 0xee);
}

void vgaputim(int16_t x, int16_t y, int16_t ch, int16_t w, int16_t h)
{
	SDL_Surface *tmp;
	SDL_Surface *mask;
	SDL_Surface *scr = NULL;
	uint8_t   *tmp_pxl, *mask_pxl, *scr_pxl;
	int16_t realsize;
	int16_t i;

	tmp = ch2bmap(sprites[ch*2], w, h);
	mask = ch2bmap(sprites[ch*2+1], w, h);
	vgageti(x, y, (uint8_t *)&scr, w, h);
	realsize = scr->w * scr->h;
	tmp_pxl = (uint8_t *)tmp->pixels;
	mask_pxl = (uint8_t *)mask->pixels;
	scr_pxl = (uint8_t *)scr->pixels;
	for(i=0;i<realsize;i++)
		if(tmp_pxl[i] != 0xff)
			scr_pxl[i] = (scr_pxl[i] & mask_pxl[i]) | \
				tmp_pxl[i];

	vgaputi(x, y, (uint8_t *)&scr, w, h);
	tmp->pixels = NULL;   /* We should NULL'ify these ppointers, or the VGLBitmapDestroy */
	mask->pixels = NULL;  /* will shoot itself in the foot by trying to dellocate statically */
	SDL_FreeSurface(tmp);/* allocated arrays */
	SDL_FreeSurface(mask);
	SDL_FreeSurface(scr);
}

void vgawrite(int16_t x, int16_t y, int16_t ch, int16_t c)
{
	SDL_Surface *tmp;
	uint8_t *copy;
	uint8_t color;
	int16_t w=3, h=12, size;
	int16_t i;

	if(((ch - 32) >= 0x5f) || (ch < 32))
		return;
	tmp = ch2bmap(alphas[ch-32], w, h);
	size = tmp->w*tmp->h;
	copy = malloc(size);
	memcpy(copy, tmp->pixels, size);

	for(i = size;i!=0;) {
		i--;
		color = copy[i];
		if (color==10) {
			if (c==2)
				color=12;
            		else {
              			if (c==3)
				color=14;
			}
          	} else
			if (color==12) {
				if (c==1)
					color=2;
				else
					if (c==2)
						color=4;
					else
						if (c==3)
							color=6;
			}
		copy[i] = color;
	}
	tmp->pixels = copy;
	vgaputi(x, y, (uint8_t *)&tmp, w, h);
	SDL_FreeSurface(tmp);
	free(copy);
}

void vgatitle(void)
{
	SDL_Surface *tmp=NULL;

	vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
	gettitle(tmp->pixels);
	vgaputi(0, 0, (uint8_t *)&tmp, 80, 200);
	SDL_FreeSurface(tmp);
}

void gretrace(void)
{
}

void savescreen(void)
{
/*	FILE *f;
	int i;
	
	f=fopen("screen.saw", "w");
	
	for(i=0;i<(VGLDisplay->Xsize*VGLDisplay->Ysize);i++)
		fputc(VGLDisplay->Bitmap[i], f);
	fclose(f);*/
}

void
sdl_enable_fullscreen(void)
{

#if defined(SDL_OLD)
  addflag |= SDL_FULLSCREEN;
#endif
}

void
sdl_set_x11_parent(unsigned int xp)
{

  x11_parent = (Window)xp;
}


/* 
 * Depreciated functions, necessary only to avoid "Undefined symbol:..." compiler
 * errors.
 */
 
void cgainit(void) {}
void cgaclear(void) {}
void cgatitle(void) {}
void cgawrite(int16_t x, int16_t y, int16_t ch, int16_t c) {}
void cgaputim(int16_t x, int16_t y, int16_t ch, int16_t w, int16_t h) {}
void cgageti(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {}
void cgaputi(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {}
void cgapal(int16_t pal) {}
void cgainten(int16_t inten) {}
int16_t cgagetpix(int16_t x, int16_t y) {return(0);}
