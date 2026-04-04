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

#include <stdio.h>
/* malloc() and friends */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Lovely SDL */
#include <SDL.h>
#include <SDL_syswm.h>

#include "alpha.h"
#if defined(__EMSCRIPTEN__)
#include "ems_vid.h"
#endif
#include "def.h"
#include "hardware.h"
#include "title_gz.h"
#include "icon.h"
#include "sdl_vid.h"

extern const uint8_t *vgatable[];
extern bool use_async_screen_updates;

static const int16_t xratio = 2;
static const int16_t yratio = 2;
static const int16_t yoffset = 0;
static const int16_t hratio = 2;
static const int16_t wratio = 2 * 4;
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 400
#define virt2scrx(x) (x*xratio)
#define virt2scry(y) (y*yratio+yoffset)
#define virt2scrw(w) (w*wratio)
#define virt2scrh(h) (h*hratio)

/* palette1, normal intensity */
static const SDL_Color vga16_pal1[] = \
{{0,0,0,0},{0,0,128,0},{0,128,0,0},{0,128,128,0},{128,0,0,0},{128,0,128,0} \
,{128,64,0,0},{128,128,128,0},{64,64,64,0},{0,0,255,0},{0,255,0,0} \
,{0,255,255,0},{255,0,0,0},{255,0,255,0},{255,255,0,0},{255,255,255,0}};
/* palette1, high intensity */
static const SDL_Color vga16_pal1i[] = \
{{0,0,0,0},{0,0,255,0},{0,255,0,0},{0,255,255,0},{255,0,0,0},{255,0,255,0} \
,{255,128,0,0},{196,196,196,0},{128,128,128,0},{128,128,255,0},{128,255,128,0} \
,{128,255,255,0},{255,128,128,0},{255,128,255,0},{255,255,128,0},{255,255,255,0}};
/* palette2, normal intensity */
static const SDL_Color vga16_pal2[] = \
{{0,0,0,0},{0,128,0,0},{128,0,0,0},{128,64,0,0},{0,0,128,0},{0,128,128,0} \
,{128,0,128,0},{128,128,128,0},{64,64,64,0},{0,255,0,0},{255,0,0,0} \
,{255,255,0,0},{0,0,255,0},{0,255,255,0},{255,0,255,0},{255,255,255,0}};
/* palette2, high intensity */
static const SDL_Color vga16_pal2i[] = \
{{0,0,0,0},{0,255,0,0},{255,0,0,0},{255,128,0,0},{0,0,255,0},{0,255,255,0} \
,{255,0,255,0},{196,196,196,0},{128,128,128,0},{128,255,128,0},{255,128,128,0} \
,{255,255,128,0},{128,128,255,0},{128,255,255,0},{255,128,255,0},{255,255,255,0}};

static const SDL_Color *npalettes[] = {vga16_pal1, vga16_pal2};
static const SDL_Color *ipalettes[] = {vga16_pal1i, vga16_pal2i};
static int16_t	currpal=0;

#if defined(HAVE_SDL_X11_WINDOW)
static Window x11_parent = 0;
#endif

#define SDL_FULLSCREEN (0x1 << 0)
static uint32_t	addflag=0;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *roottxt = NULL;
static SDL_Surface *screen = NULL;
static SDL_Surface *screen16 = NULL;

struct sdl_display_state {
	SDL_Thread *thread;
	SDL_mutex *queue_lock;
	SDL_cond *queue_cv;
	bool sync_ready;
	bool thread_started;
	bool stop;
	bool pending;
	bool mode_change_pending;
	uint32_t mode_addflag;
	uint32_t mode_prev_addflag;
	SDL_Surface *pending16;
	SDL_Surface *work16;
};

static struct sdl_display_state display = {0};

struct ch2bmap_plane {
	uint8_t const * const *sprites;
	SDL_Surface *caches[256];
};

static struct ch2bmap_plane sprites = {.sprites = vgatable};
static struct ch2bmap_plane alphas = {.sprites = ascii2vga};

static bool display_sync_init(void);
static void display_sync_destroy(void);
static int display_thread_main(void *arg);
static bool display_async_start(void);
static void display_async_discard(void);
static void display_async_stop(void);
static void display_present_pixels_locked(const void *pixels);
static void display_present_frame(SDL_Surface *frame16);
static void display_submit_frame(void);
static bool switchmode_apply(uint32_t desired_addflag, uint32_t fallback_addflag);
static bool setmode(void);

static SDL_Surface *
ch2bmap(struct ch2bmap_plane *planep, uint8_t sprite, int16_t w, int16_t h)
{
	int16_t realw, realh;
	SDL_Surface *tmp;
        const uint8_t *sp;

	if (planep->caches[sprite] != NULL) {
		return (planep->caches[sprite]);
	}
	realw = virt2scrw(w);
	realh = virt2scrh(h);
        sp = planep->sprites[sprite];
	tmp = SDL_CreateRGBSurfaceFrom((void *)sp, realw, realh, 8, realw, 0, 0, 0, 0);
	SDL_SetPaletteColors(tmp->format->palette, npalettes[0], 0, 16);
	planep->caches[sprite] = tmp;
	return(tmp);
}

void graphicsoff(void)
{
	if (display.thread_started)
		display_async_stop();
	display_sync_destroy();
	if (screen16 != NULL) {
		SDL_FreeSurface(screen16);
		screen16 = NULL;
	}
	if (screen != NULL) {
		SDL_FreeSurface(screen);
		screen = NULL;
	}
	if (roottxt != NULL) {
		SDL_DestroyTexture(roottxt);
		roottxt = NULL;
	}
	if (renderer != NULL) {
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
	if (window != NULL) {
		SDL_DestroyWindow(window);
		window = NULL;
	}
	SDL_Quit();
}

#if defined(HAVE_SDL_X11_WINDOW)
static void
x11_set_parent(Window parent)
{
	SDL_SysWMinfo sdl_info;

	SDL_VERSION(&sdl_info.version);
	if (SDL_GetWindowWMInfo(window, &sdl_info) != SDL_TRUE) {
		fprintf(stderr, "SDL_GetWindowWMInfo() failed: %s\n", SDL_GetError());
		return;
	}
	if (sdl_info.subsystem != SDL_SYSWM_X11) {
		fprintf(stderr, "SDL window manager is not X11\n");
		return;
	}
	XReparentWindow(sdl_info.info.x11.display, sdl_info.info.x11.window, parent, 0, 0);
	XSync(sdl_info.info.x11.display, False);
}
#endif

static bool
display_sync_init(void)
{
	SDL_mutex *queue_lock;
	SDL_cond *queue_cv;

	assert(!display.sync_ready);
	queue_lock = SDL_CreateMutex();
	queue_cv = SDL_CreateCond();
	if (queue_lock == NULL || queue_cv == NULL) {
		fprintf(stderr, "Failed to initialize display sync objects: %s\n",
		    SDL_GetError());
		if (queue_cv != NULL)
			SDL_DestroyCond(queue_cv);
		if (queue_lock != NULL)
			SDL_DestroyMutex(queue_lock);
		return (false);
	}
	display.queue_lock = queue_lock;
	display.queue_cv = queue_cv;
	display.sync_ready = true;
	return (true);
}

static void
display_sync_destroy(void)
{
	if (!display.sync_ready)
		return;
	SDL_DestroyCond(display.queue_cv);
	display.queue_cv = NULL;
	SDL_DestroyMutex(display.queue_lock);
	display.queue_lock = NULL;
	display.sync_ready = false;
}

static void
display_present_pixels_locked(const void *pixels)
{
	SDL_UpdateTexture(roottxt, NULL, pixels, screen->pitch);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, roottxt, NULL, NULL);
	SDL_RenderPresent(renderer);
}

static void
display_present_frame(SDL_Surface *frame16)
{
	SDL_BlitSurface(frame16, NULL, screen, NULL);
	display_present_pixels_locked(screen->pixels);
}

static int
display_thread_main(void *arg)
{
	SDL_Surface *frame16;
	uint32_t desired_addflag;
	uint32_t fallback_addflag;

	(void)arg;

	for (;;) {
		SDL_LockMutex(display.queue_lock);
		while (!display.pending && !display.stop &&
		    !display.mode_change_pending)
			SDL_CondWait(display.queue_cv, display.queue_lock);
		if (!display.pending && !display.mode_change_pending && display.stop) {
			SDL_UnlockMutex(display.queue_lock);
			break;
		}
		if (display.mode_change_pending) {
			desired_addflag = display.mode_addflag;
			fallback_addflag = display.mode_prev_addflag;
			display.mode_change_pending = false;
			SDL_UnlockMutex(display.queue_lock);
			if (switchmode_apply(desired_addflag, fallback_addflag))
				display_present_frame(display.work16);
			continue;
		}
		frame16 = display.pending16;
		display.pending16 = display.work16;
		display.work16 = frame16;
		display.pending = false;
		SDL_UnlockMutex(display.queue_lock);
		display_present_frame(frame16);
	}
	return (0);
}

static bool
switchmode_apply(uint32_t desired_addflag, uint32_t fallback_addflag)
{
	addflag = desired_addflag;
	if (setmode())
		return (true);
	addflag = fallback_addflag;
	return (setmode());
}

static bool
display_async_start(void)
{
	if (display.thread_started)
		return (true);
	display.pending16 = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 8,
	    0, 0, 0, 0);
	display.work16 = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 8,
	    0, 0, 0, 0);
	if (display.pending16 == NULL || display.work16 == NULL) {
		fprintf(stderr, "Failed to allocate display queue buffers\n");
		display_async_discard();
		return (false);
	}
	SDL_SetPaletteColors(display.pending16->format->palette,
	    screen16->format->palette->colors, 0, 16);
	SDL_SetPaletteColors(display.work16->format->palette,
	    screen16->format->palette->colors, 0, 16);
	display.stop = false;
	display.pending = false;
	display.mode_change_pending = false;
	display.thread = SDL_CreateThread(display_thread_main, "digger-display",
	    NULL);
	if (display.thread == NULL) {
		fprintf(stderr, "Failed to create display thread: %s\n",
		    SDL_GetError());
		display_async_discard();
		return (false);
	}
	display.thread_started = true;
	return (true);
}

static void
display_async_discard(void)
{
	SDL_FreeSurface(display.pending16);
	display.pending16 = NULL;
	SDL_FreeSurface(display.work16);
	display.work16 = NULL;
	display.thread = NULL;
	display.thread_started = false;
	display.stop = false;
	display.pending = false;
	display.mode_change_pending = false;
}

static void
display_async_stop(void)
{
	assert(display.thread_started);
	assert(display.thread != NULL);
	SDL_LockMutex(display.queue_lock);
	display.stop = true;
	SDL_CondSignal(display.queue_cv);
	SDL_UnlockMutex(display.queue_lock);
	SDL_WaitThread(display.thread, NULL);
	display_async_discard();
}

static void
display_submit_frame(void)
{
	SDL_LockMutex(display.queue_lock);
	SDL_SetPaletteColors(display.pending16->format->palette,
	    screen16->format->palette->colors, 0, 16);
	SDL_BlitSurface(screen16, NULL, display.pending16, NULL);
	display.pending = true;
	SDL_CondSignal(display.queue_cv);
	SDL_UnlockMutex(display.queue_lock);
}

static bool
setmode(void)
{
#if defined(__EMSCRIPTEN__)
	return (true);
#else
#if defined(SDL_OLD)
#if defined(HAVE_SDL_X11_WINDOW)
        static int x11_parent_inited = 0;

        if (x11_parent && x11_parent_inited == 0) {
                addflag |= SDL_NOFRAME;
        }
#endif
        if (screen != NULL)
            SDL_FreeSurface(screen);
        screen = SDL_SetVideoMode(640, 400, 8, SDL_HWSURFACE | SDL_HWPALETTE | \
          SDL_DOUBLEBUF | addflag);
        if (screen == NULL)
		return(false);
#if defined(HAVE_SDL_X11_WINDOW)
        if (x11_parent && x11_parent_inited == 0) {
                x11_set_parent(x11_parent);
        }
#endif
#endif
        if ((addflag & SDL_FULLSCREEN) != 0) {
                SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
                SDL_SetWindowFullscreen(window, 0);
        }
        if (renderer != NULL) {
                SDL_DestroyRenderer(renderer);
        }
        if (roottxt != NULL) {
                SDL_DestroyTexture(roottxt);
        }
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (renderer == NULL) {
                fprintf(stderr, "SDL_CreateRenderer() failed: %s\n",
                    SDL_GetError());
                return (false);
        }
        roottxt = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
             SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
        if (roottxt == NULL) {
                fprintf(stderr, "SDL_CreateTexture() failed: %s\n",
                    SDL_GetError());
                return (false);
        }
	return(true);
#endif
}

void switchmode(void)
{
#if defined(__EMSCRIPTEN__)
	ems_vid_switchmode();
#else
	uint32_t saved;

	saved = addflag;
	if ((addflag & SDL_FULLSCREEN) == 0) {
		addflag |= SDL_FULLSCREEN;
	} else {
		addflag &= ~SDL_FULLSCREEN;
        }
	if (display.thread_started) {
		SDL_LockMutex(display.queue_lock);
		display.mode_prev_addflag = saved;
		display.mode_addflag = addflag;
		display.mode_change_pending = true;
		SDL_CondSignal(display.queue_cv);
		SDL_UnlockMutex(display.queue_lock);
		return;
	}
	if (!switchmode_apply(addflag, saved)) {
		addflag = saved;
		fprintf(stderr, "Fatal: failed to change videomode and"\
			"fallback mode failed as well. Exitting.\n");
		exit(1);
	}
#endif
}


void vgainit(void)
{
	SDL_Surface *wm_icon;
	uint32_t window_flags = 0;
	
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",
                    SDL_GetError());
		exit(1);
	}

#if defined(HAVE_SDL_X11_WINDOW)
	if (x11_parent != 0) {
		window_flags |= SDL_WINDOW_BORDERLESS;
	}
#endif
        window = SDL_CreateWindow("D I G G E R", SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, window_flags);
        if (window == NULL) {
                fprintf(stderr, "SDL_CreateWindow() failed: %s\n",
                    SDL_GetError());
                exit(1);
        }
#if defined(__EMSCRIPTEN__)
	ems_vid_init(window, &addflag, SCREEN_WIDTH, SCREEN_HEIGHT);
#endif
#if defined(HAVE_SDL_X11_WINDOW)
	if (x11_parent != 0) {
		x11_set_parent(x11_parent);
	}
#endif

        wm_icon = SDL_CreateRGBSurfaceFrom(Icon, 64, 64, 32, 64 * 4,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (wm_icon != NULL) {
                SDL_SetWindowIcon(window, wm_icon);
                SDL_FreeSurface(wm_icon);
        }
        screen = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (screen == NULL) {
                fprintf(stderr, "SDL_CreateRGBSurface() failed: %s\n",
                    SDL_GetError());
                exit(1);
        }
        screen16 = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 8, 0,
            0, 0, 0);
        if (screen16 == NULL) {
                fprintf(stderr, "SDL_CreateRGBSurface() failed: %s\n",
                    SDL_GetError());
                exit(1);
        }
        if (setmode() == false) {
                fprintf(stderr, "Couldn't set 640x400x8 video mode: %s\n",
                    SDL_GetError());
                exit(1);
        }
	if (!display_sync_init())
		exit(1);

#if defined(__EMSCRIPTEN__)
	use_async_screen_updates = false;
#else
	if (use_async_screen_updates && !display_async_start()) {
		fprintf(stderr, "Falling back to synchronous screen updates\n");
		use_async_screen_updates = false;
	}
#endif
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

static void
setpal(const SDL_Color *pal)
{

	SDL_SetPaletteColors(screen16->format->palette, pal, 0, 16);
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
        if (display.thread_started) {
                display_submit_frame();
                return;
        }
        SDL_BlitSurface(screen16, NULL, screen, NULL);
        display_present_pixels_locked(screen->pixels);
}

void vgaputi(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h)
{
	SDL_Surface *tmp;
	SDL_Palette *reserv;
        SDL_Rect rect;

	rect.x = virt2scrx(x);
	rect.y = virt2scry(y);
	rect.w = virt2scrw(w);
	rect.h = virt2scrh(h);

	memcpy(&tmp, p, (sizeof (SDL_Surface *)));
	reserv = tmp->format->palette;
	tmp->format->palette = screen16->format->palette;
	SDL_BlitSurface(tmp, NULL, screen16, &rect);
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
	src.w = virt2scrw(w);
	src.h = virt2scrh(h);

	tmp = SDL_CreateRGBSurface(0, src.w, src.h, 8, 0, 0, 0, 0);
	SDL_SetPaletteColors(tmp->format->palette, screen16->format->palette->colors, 0,
            screen16->format->palette->ncolors);
	SDL_BlitSurface(screen16, &src, tmp, NULL);
	memcpy(p, &tmp, (sizeof (SDL_Surface *)));
}

int16_t vgagetpix(int16_t x, int16_t y)
{
	int16_t sx, sy, sw, sh;
	int16_t xi, yi;
	int16_t rval = 0;
	uint8_t *base;

	if ((x > 319) || (y > 199))
	       return (0xff);

	/* Read directly from screen16 instead of allocating a temporary
	 * surface per call. virt2scr maps virtual 1x1 to 8x2 real pixels. */
	sx = virt2scrx(x);
	sy = virt2scry(y);
	sw = virt2scrw(1);
	sh = virt2scrh(1);

	base = (uint8_t *)screen16->pixels + sy * screen16->pitch + sx;
	for (yi = 0; yi < sh; yi++)
		for (xi = 0; xi < sw; xi++)
			if (base[yi * screen16->pitch + xi])
				rval |= 0x80 >> xi;

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

	tmp = ch2bmap(&sprites, ch*2, w, h);
	mask = ch2bmap(&sprites, ch*2+1, w, h);
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
	SDL_FreeSurface(scr);
}

void vgawrite(int16_t x, int16_t y, int16_t ch, int16_t c)
{
	SDL_Surface *tmp;
	uint8_t copy_buf[24 * 24];
	uint8_t *orig, *copy;
	uint8_t color;
	int16_t w=3, h=12, size;
	int16_t i;

	if (!isvalchar(ch))
		return;
	tmp = ch2bmap(&alphas, ch-32, w, h);
	size = tmp->w*tmp->h;
	/* Max size: virt2scrw(3) * virt2scrh(12) = 24 * 24 = 576 bytes. */
	copy = copy_buf;
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
	orig = (uint8_t*)tmp->pixels;
	tmp->pixels = copy;
	vgaputi(x, y, (uint8_t *)&tmp, w, h);
	tmp->pixels = orig;
}

void vgatitle(void)
{
	SDL_Surface *tmp=NULL;

	vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
	gettitle((uint8_t*)tmp->pixels);
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
#if !defined(__EMSCRIPTEN__)
  addflag |= SDL_FULLSCREEN;
#endif
}

#if defined(HAVE_SDL_X11_WINDOW)
void
sdl_set_x11_parent(unsigned int xp)
{

  x11_parent = (Window)xp;
}
#endif


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
