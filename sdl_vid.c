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
#include <stdlib.h>
#include <math.h>

/* Lovely SDL */
#include <SDL.h>
#include <SDL_syswm.h>

#include "alpha.h"
#include "def.h"
#include "digger.h"
#include "drawing.h"
#include "hardware.h"
#include "icon.h"
#include "ini.h"
#include "sdl_vid.h"
#include "sound.h"
#include "title_gz.h"

extern const uint8_t *vgatable[];

static const int16_t xratio = 2;
static const int16_t yratio = 2;
static const int16_t yoffset = 0;
static const int16_t hratio = 2;
static const int16_t wratio = 2 * 4;
#define virt2scrx(x) (x * xratio)
#define virt2scry(y) (y * yratio + yoffset)
#define virt2scrw(w) (w * wratio)
#define virt2scrh(h) (h * hratio)

/* palette1, normal intensity */
static const SDL_Color vga16_pal1[] = {
    {0, 0, 0, 0},    {0, 0, 128, 0},   {0, 128, 0, 0},   {0, 128, 128, 0},
    {128, 0, 0, 0},  {128, 0, 128, 0}, {128, 64, 0, 0},  {128, 128, 128, 0},
    {64, 64, 64, 0}, {0, 0, 255, 0},   {0, 255, 0, 0},   {0, 255, 255, 0},
    {255, 0, 0, 0},  {255, 0, 255, 0}, {255, 255, 0, 0}, {255, 255, 255, 0}};
/* palette1, high intensity */
static const SDL_Color vga16_pal1i[] = {
    {0, 0, 0, 0},       {0, 0, 255, 0},     {0, 255, 0, 0},
    {0, 255, 255, 0},   {255, 0, 0, 0},     {255, 0, 255, 0},
    {255, 128, 0, 0},   {196, 196, 196, 0}, {128, 128, 128, 0},
    {128, 128, 255, 0}, {128, 255, 128, 0}, {128, 255, 255, 0},
    {255, 128, 128, 0}, {255, 128, 255, 0}, {255, 255, 128, 0},
    {255, 255, 255, 0}};
/* palette2, normal intensity */
static const SDL_Color vga16_pal2[] = {
    {0, 0, 0, 0},    {0, 128, 0, 0},   {128, 0, 0, 0},   {128, 64, 0, 0},
    {0, 0, 128, 0},  {0, 128, 128, 0}, {128, 0, 128, 0}, {128, 128, 128, 0},
    {64, 64, 64, 0}, {0, 255, 0, 0},   {255, 0, 0, 0},   {255, 255, 0, 0},
    {0, 0, 255, 0},  {0, 255, 255, 0}, {255, 0, 255, 0}, {255, 255, 255, 0}};
/* palette2, high intensity */
static const SDL_Color vga16_pal2i[] = {
    {0, 0, 0, 0},       {0, 255, 0, 0},     {255, 0, 0, 0},
    {255, 128, 0, 0},   {0, 0, 255, 0},     {0, 255, 255, 0},
    {255, 0, 255, 0},   {196, 196, 196, 0}, {128, 128, 128, 0},
    {128, 255, 128, 0}, {255, 128, 128, 0}, {255, 255, 128, 0},
    {128, 128, 255, 0}, {128, 255, 255, 0}, {255, 128, 255, 0},
    {255, 255, 255, 0}};

static const SDL_Color *npalettes[] = {vga16_pal1, vga16_pal2};
static const SDL_Color *ipalettes[] = {vga16_pal1i, vga16_pal2i};
static int16_t currpal = 0;

#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
static Window x11_parent = 0;
#endif

#define SDL_FULLSCREEN (0x1 << 0)
static uint32_t addflag = 0;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *roottxt = NULL;
static SDL_Surface *screen16 = NULL;

/* Precomputed ARGB palette for fast conversion */
static uint32_t argb_palette[16];

/* Rendering options */
static int use_integer_scaling = 1;
static int use_linear_filter = 0;
static int use_scanlines = 1;
static int scanline_intensity = 55; /* 0-100, how dark scanlines are */
static SDL_Texture *scanline_overlay = NULL;

/* Bloom effect — downsample→blur→upsample pipeline */
static int use_bloom = 1;
static SDL_Texture *bloom_texture = NULL; /* kept for API compat, unused */
static SDL_Texture *bloom_down = NULL;    /* 160x100 downsample */
static SDL_Texture *bloom_blur_a = NULL;  /* 160x100 horizontal blur */
static SDL_Texture *bloom_blur_b = NULL;  /* 160x100 vertical blur */

static int use_crt_mask = 1;
static SDL_Texture *crt_mask_texture = NULL;

/* Dynamic Lighting */
static int use_lighting = 1;
static SDL_Texture *light_map = NULL;
static SDL_Texture *light_sprite = NULL;
static SDL_Texture *map_mask = NULL;
static int light_map_dirty = 1;

/* Palette fade interpolation */
static int use_palette_fade = 1;
static uint32_t fade_from[16];
static uint32_t fade_to[16];
static float fade_progress = 1.0f; /* 1.0 = complete, no fade active */
static double fade_start_time_ms = 0.0;
static double fade_duration_ms = 0.0;
static double vid_perf_counter_to_ms = 0.0;

/* Frame interpolation */
static int use_frame_interp = 0;
static SDL_Texture *prev_frame = NULL;
static SDL_Texture *curr_frame = NULL;
static int frame_interp_ready = 0; /* Need 2 commits before interpolation */

/* Forward declarations */
static void update_argb_palette(const SDL_Color *pal);
static void create_scanline_overlay(void);
static void create_bloom_texture(void);
static void create_crt_mask_texture(void);
static void create_light_map_texture(void);
static void create_light_sprite_texture(void);
static void create_map_mask_texture(void);
static void update_map_mask(void);

struct ch2bmap_plane {
  uint8_t const *const *sprites;
  SDL_Surface *caches[256];
};

static struct ch2bmap_plane sprites = {.sprites = vgatable};
static struct ch2bmap_plane alphas = {.sprites = ascii2vga};

static SDL_Surface *ch2bmap(struct ch2bmap_plane *planep, uint8_t sprite,
                            int16_t w, int16_t h) {
  int16_t realw, realh;
  SDL_Surface *tmp;
  const uint8_t *sp;

  if (planep->caches[sprite] != NULL) {
    return (planep->caches[sprite]);
  }
  realw = virt2scrw(w);
  realh = virt2scrh(h);
  sp = planep->sprites[sprite];
  tmp =
      SDL_CreateRGBSurfaceFrom((void *)sp, realw, realh, 8, realw, 0, 0, 0, 0);
  SDL_SetPaletteColors(tmp->format->palette, npalettes[0], 0, 16);
  planep->caches[sprite] = tmp;
  return (tmp);
}

void graphicsoff(void) {}

static bool setmode(void) {
#if defined(SDL_OLD)
#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
  static int x11_parent_inited = 0;

  if (x11_parent && x11_parent_inited == 0) {
    addflag |= SDL_NOFRAME;
  }
#endif
  screen = SDL_SetVideoMode(
      640, 400, 8, SDL_HWSURFACE | SDL_HWPALETTE | SDL_DOUBLEBUF | addflag);
  if (screen == NULL)
    return (false);
#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
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
  return (true);
}

void switchmode(void) {
  uint32_t saved;

  saved = addflag;

  if ((addflag & SDL_FULLSCREEN) == 0) {
    addflag |= SDL_FULLSCREEN;
  } else {
    addflag &= ~SDL_FULLSCREEN;
  }
  if (setmode() == false) {
    addflag = saved;
    if (setmode() == false) {
      fprintf(stderr, "Fatal: failed to change videomode and"
                      "fallback mode failed as well. Exitting.\n");
      exit(1);
    }
  }
}

void vgainit(void) {
  SDL_Surface *wm_icon;
  int window_w, window_h;

  /* Load graphics settings from INI */
  use_integer_scaling =
      GetINIBool(INI_GRAPHICS_SETTINGS, "IntegerScale", true, ININAME);
  use_linear_filter =
      GetINIBool(INI_GRAPHICS_SETTINGS, "LinearFilter", true, ININAME);
  use_scanlines = GetINIBool(INI_GRAPHICS_SETTINGS, "Scanlines", true, ININAME);
  scanline_intensity =
      GetINIInt(INI_GRAPHICS_SETTINGS, "ScanlineLevel", 55, ININAME);
  use_bloom = GetINIBool(INI_GRAPHICS_SETTINGS, "Bloom", true, ININAME);
  use_crt_mask = GetINIBool(INI_GRAPHICS_SETTINGS, "CRTMask", true, ININAME);
  use_lighting = GetINIBool(INI_GRAPHICS_SETTINGS, "Lighting", true, ININAME);
  use_palette_fade =
      GetINIBool(INI_GRAPHICS_SETTINGS, "PaletteFade", true, ININAME);

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    exit(1);
  }

  /* Start with 2x scale (1280x800), user can resize */
  window_w = 640 * 2;
  window_h = 400 * 2;

  window = SDL_CreateWindow("D I G G E R", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, window_w, window_h,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (window == NULL) {
    fprintf(stderr, "SDL_CreateWindow() failed: %s\n", SDL_GetError());
    exit(1);
  }

  wm_icon = SDL_CreateRGBSurfaceFrom(Icon, 64, 64, 32, 64 * 4, 0x00FF0000,
                                     0x0000FF00, 0x000000FF, 0xFF000000);
  if (wm_icon != NULL) {
    SDL_SetWindowIcon(window, wm_icon);
    SDL_FreeSurface(wm_icon);
  }

  /* Use accelerated renderer without vsync — game tick timer handles pacing.
   * VSync blocking in SDL_RenderPresent() causes unpredictable delays that
   * fight with the fixed-step game timer, producing micro-stutters when the
   * tick period (28 ms) does not align with the display refresh (16.67 ms). */
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL) {
    /* Fallback to software renderer */
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (renderer == NULL) {
    fprintf(stderr, "SDL_CreateRenderer() failed: %s\n", SDL_GetError());
    exit(1);
  }

  /* Set up logical rendering at 640x400 (internal resolution) */
  SDL_RenderSetLogicalSize(renderer, 640, 400);

  /* Integer scaling for pixel-perfect rendering */
  if (use_integer_scaling) {
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
  }

  /* Texture filtering: nearest (sharp pixels) or linear (smooth) */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              use_linear_filter ? "linear" : "nearest");

  roottxt = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, 640, 400);
  if (roottxt == NULL) {
    fprintf(stderr, "SDL_CreateTexture() failed: %s\n", SDL_GetError());
    exit(1);
  }

  /* Frame interpolation textures */
  use_frame_interp =
      GetINIBool(INI_GRAPHICS_SETTINGS, "FrameInterp", false, ININAME);
  prev_frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET, 640, 400);
  curr_frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET, 640, 400);

  if (use_bloom)
    create_bloom_texture();
  if (use_crt_mask)
    create_crt_mask_texture();
  if (use_lighting) {
    create_light_map_texture();
    create_light_sprite_texture();
    create_map_mask_texture();
  }

  screen16 = SDL_CreateRGBSurface(0, 640, 400, 8, 0, 0, 0, 0);
  if (screen16 == NULL) {
    fprintf(stderr, "SDL_CreateRGBSurface() failed: %s\n", SDL_GetError());
    exit(1);
  }

  /* Initialize palette lookup table */
  update_argb_palette(npalettes[0]);

  /* Cache performance counter frequency for fade timing */
  {
    uint64_t freq = SDL_GetPerformanceFrequency();
    vid_perf_counter_to_ms = (freq == 0) ? 1.0 : 1000.0 / (double)freq;
  }

  /* Create scanline overlay if enabled */
  if (use_scanlines) {
    create_scanline_overlay();
  }

  if (setmode() == false) {
    fprintf(stderr, "Couldn't set video mode: %s\n", SDL_GetError());
    exit(1);
  }
  SDL_ShowCursor(0);
}

void vgaclear(void) {
  SDL_Surface *tmp = NULL;

  vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
  memset(tmp->pixels, 0x00, tmp->w * tmp->h);
  vgaputi(0, 0, (uint8_t *)&tmp, 80, 200);
  SDL_FreeSurface(tmp);
}

static void update_argb_palette(const SDL_Color *pal) {
  int i;
  for (i = 0; i < 16; i++) {
    argb_palette[i] =
        (0xFFu << 24) | (pal[i].r << 16) | (pal[i].g << 8) | pal[i].b;
  }
}

static void setpal(const SDL_Color *pal) {
  SDL_SetPaletteColors(screen16->format->palette, pal, 0, 16);
  update_argb_palette(pal);
  fade_progress = 1.0f; /* Cancel any in-progress fade */
}

static void create_scanline_overlay(void) {
  uint32_t *pixels;
  int pitch;
  int y;
  uint8_t alpha;

  if (renderer == NULL)
    return;

  if (scanline_overlay != NULL) {
    SDL_DestroyTexture(scanline_overlay);
  }

  scanline_overlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, 640, 400);
  if (scanline_overlay == NULL)
    return;

  SDL_SetTextureBlendMode(scanline_overlay, SDL_BLENDMODE_BLEND);

  /* Create horizontal scanline pattern */
  SDL_LockTexture(scanline_overlay, NULL, (void **)&pixels, &pitch);

  alpha = (uint8_t)(scanline_intensity * 255 / 100);

  for (y = 0; y < 400; y++) {
    uint32_t color;
    int x;

    /* Every other line is darkened */
    if (y % 2 == 1) {
      color = ((uint32_t)alpha << 24) | 0x000000; /* Semi-transparent black */
    } else {
      color = 0x00000000; /* Fully transparent */
    }

    for (x = 0; x < 640; x++) {
      pixels[y * (pitch / 4) + x] = color;
    }
  }

  SDL_UnlockTexture(scanline_overlay);
}

void vgainten(int16_t inten) {
  if (inten == 1)
    setpal(ipalettes[currpal]);
  else
    setpal(npalettes[currpal]);
}

void vgapal(int16_t pal) {
  setpal(npalettes[pal]);
  currpal = pal;
}

void sdl_fade_to_intensity(int inten, int duration_ms) {
  int i;
  const SDL_Color *target;

  if (!use_palette_fade || duration_ms <= 0) {
    vgainten(inten);
    return;
  }

  /* Snapshot current rendered palette as "from" */
  for (i = 0; i < 16; i++)
    fade_from[i] = argb_palette[i];

  /* Compute target palette */
  target = (inten == 1) ? ipalettes[currpal] : npalettes[currpal];
  for (i = 0; i < 16; i++)
    fade_to[i] =
        (0xFFu << 24) | (target[i].r << 16) | (target[i].g << 8) | target[i].b;

  /* Set the palette immediately on screen16 so sprites draw with target */
  SDL_SetPaletteColors(screen16->format->palette, target, 0, 16);

  fade_progress = 0.0f;
  fade_duration_ms = (double)duration_ms;
  fade_start_time_ms =
      (double)SDL_GetPerformanceCounter() * vid_perf_counter_to_ms;
}

/* Convert screen16 (8-bit indexed) → roottxt (ARGB streaming texture).
 * Handles palette fade interpolation. No rendering or present. */
static void flush_screen16_to_roottxt(void) {
  void *texture_pixels;
  int texture_pitch;
  uint8_t *src_row;
  uint32_t *dest_row;
  int x, y;
  int src_pitch = screen16->pitch;
  int dest_pitch_pixels;
  uint32_t render_palette[16];
  const uint32_t *pal;

  /* Compute interpolated palette if fade is in progress */
  if (fade_progress < 1.0f && use_palette_fade) {
    double now_ms =
        (double)SDL_GetPerformanceCounter() * vid_perf_counter_to_ms;
    float t = (float)((now_ms - fade_start_time_ms) / fade_duration_ms);
    if (t >= 1.0f) {
      t = 1.0f;
      fade_progress = 1.0f;
      /* Set final palette */
      for (x = 0; x < 16; x++)
        argb_palette[x] = fade_to[x];
    } else {
      fade_progress = t;
      /* Lerp each channel */
      for (x = 0; x < 16; x++) {
        uint32_t fr = fade_from[x];
        uint32_t to = fade_to[x];
        uint8_t r = (uint8_t)((float)((fr >> 16) & 0xFF) * (1.0f - t) +
                               (float)((to >> 16) & 0xFF) * t);
        uint8_t g = (uint8_t)((float)((fr >> 8) & 0xFF) * (1.0f - t) +
                               (float)((to >> 8) & 0xFF) * t);
        uint8_t b = (uint8_t)((float)(fr & 0xFF) * (1.0f - t) +
                               (float)(to & 0xFF) * t);
        render_palette[x] = (0xFFu << 24) | (r << 16) | (g << 8) | b;
      }
    }
  }

  pal = (fade_progress < 1.0f) ? render_palette : argb_palette;

  SDL_LockTexture(roottxt, NULL, &texture_pixels, &texture_pitch);
  dest_pitch_pixels = texture_pitch / 4;

  src_row = (uint8_t *)screen16->pixels;
  dest_row = (uint32_t *)texture_pixels;

  for (y = 0; y < 400; y++) {
    for (x = 0; x < 640; x++) {
      dest_row[x] = pal[src_row[x] & 0x0F];
    }
    src_row += src_pitch;
    dest_row += dest_pitch_pixels;
  }
  SDL_UnlockTexture(roottxt);
}

/* Shared post-processing: bloom, lighting, CRT mask, scanlines.
 * Called after the base frame is already rendered to the default target. */
static void apply_post_effects(void) {
  /* Apply Bloom Effect — downsample→blur→upsample */
  if (use_bloom && bloom_down != NULL && bloom_blur_a != NULL &&
      bloom_blur_b != NULL) {
    static const int h_off[] = {-4, -2, 0, 2, 4};
    static const int v_off[] = {-4, -2, 0, 2, 4};
    static const uint8_t weights[] = {20, 40, 55, 40, 20};
    int bi;

    /* Temporarily disable logical size for render target operations */
    SDL_RenderSetLogicalSize(renderer, 0, 0);

    /* Step 1: Downsample roottxt (640x400) → bloom_down (160x100) */
    SDL_SetTextureBlendMode(roottxt, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(roottxt, 255);
    SDL_SetRenderTarget(renderer, bloom_down);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, roottxt, NULL, NULL);

    /* Step 2: Horizontal blur: bloom_down → bloom_blur_a */
    SDL_SetTextureBlendMode(bloom_down, SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(renderer, bloom_blur_a);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (bi = 0; bi < 5; bi++) {
      SDL_Rect dst = {h_off[bi], 0, 160, 100};
      SDL_SetTextureAlphaMod(bloom_down, weights[bi]);
      SDL_RenderCopy(renderer, bloom_down, NULL, &dst);
    }

    /* Step 3: Vertical blur: bloom_blur_a → bloom_blur_b */
    SDL_SetTextureBlendMode(bloom_blur_a, SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(renderer, bloom_blur_b);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    for (bi = 0; bi < 5; bi++) {
      SDL_Rect dst = {0, v_off[bi], 160, 100};
      SDL_SetTextureAlphaMod(bloom_blur_a, weights[bi]);
      SDL_RenderCopy(renderer, bloom_blur_a, NULL, &dst);
    }

    /* Reset blend modes before compositing */
    SDL_SetTextureBlendMode(bloom_down, SDL_BLENDMODE_NONE);
    SDL_SetTextureBlendMode(bloom_blur_a, SDL_BLENDMODE_NONE);

    /* Restore logical size before compositing to screen */
    SDL_SetRenderTarget(renderer, NULL);
    SDL_RenderSetLogicalSize(renderer, 640, 400);

    /* Step 4: Upsample bloom_blur_b back to screen with additive blend */
    SDL_SetTextureBlendMode(bloom_blur_b, SDL_BLENDMODE_ADD);
    SDL_SetTextureAlphaMod(bloom_blur_b, 100);
    SDL_RenderCopy(renderer, bloom_blur_b, NULL, NULL);
    SDL_SetTextureBlendMode(bloom_blur_b, SDL_BLENDMODE_NONE);
  }

  /* Apply Dynamic Lighting — composite pre-built light_map */
  if (use_lighting && light_map != NULL) {
    SDL_SetTextureBlendMode(light_map, SDL_BLENDMODE_ADD);
    SDL_RenderCopy(renderer, light_map, NULL, NULL);
  }

  /* Apply CRT Shadow Mask Effect */
  if (use_crt_mask && crt_mask_texture != NULL) {
    SDL_RenderCopy(renderer, crt_mask_texture, NULL, NULL);
  }

  /* Apply scanline effect if enabled */
  if (use_scanlines && scanline_overlay != NULL) {
    SDL_RenderCopy(renderer, scanline_overlay, NULL, NULL);
  }
}

/* Rebuild the light map: clear and redraw all emerald lights.
 * Skips work if already up-to-date for the current tick. */
static void rebuild_light_map(void) {
  if (!use_lighting || light_map == NULL || !light_map_dirty)
    return;
  light_map_dirty = 0;
  SDL_SetRenderTarget(renderer, light_map);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  SDL_SetRenderTarget(renderer, NULL);
  drawemerald_lights();
}

void sdl_invalidate_light_map(void) { light_map_dirty = 1; }

void doscreenupdate(void) {
  flush_screen16_to_roottxt();
  rebuild_light_map();

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, roottxt, NULL, NULL);

  apply_post_effects();
  SDL_RenderPresent(renderer);
}

static void create_bloom_texture(void) {
  if (renderer == NULL)
    return;

  if (bloom_texture != NULL)
    SDL_DestroyTexture(bloom_texture);
  bloom_texture = NULL;

  if (bloom_down != NULL)
    SDL_DestroyTexture(bloom_down);
  if (bloom_blur_a != NULL)
    SDL_DestroyTexture(bloom_blur_a);
  if (bloom_blur_b != NULL)
    SDL_DestroyTexture(bloom_blur_b);

  /* Enable linear filtering for these textures (free bilinear downsample) */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

  bloom_down = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET, 160, 100);
  bloom_blur_a = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_TARGET, 160, 100);
  bloom_blur_b = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_TARGET, 160, 100);

  /* Restore original filter hint */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              use_linear_filter ? "linear" : "nearest");
}

static void create_crt_mask_texture(void) {
  uint32_t *pixels;
  int pitch;
  int x, y;

  if (renderer == NULL)
    return;
  if (crt_mask_texture != NULL)
    SDL_DestroyTexture(crt_mask_texture);

  crt_mask_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, 640, 400);
  if (crt_mask_texture == NULL)
    return;

  SDL_SetTextureBlendMode(crt_mask_texture, SDL_BLENDMODE_MOD);

  if (SDL_LockTexture(crt_mask_texture, NULL, (void **)&pixels, &pitch) == 0) {
    for (y = 0; y < 400; y++) {
      for (x = 0; x < 640; x++) {
        /* Subtle Trinitron-style vertical stripes */
        int x_sub = x % 3;
        uint32_t color = 0xFF000000;

        /* Use lighter tints (mostly white) for subtle modulation */
        if (x_sub == 0)
          color |= 0xFFECEC; /* Slight Red tint */
        if (x_sub == 1)
          color |= 0xECEFEC; /* Slight Green tint */
        if (x_sub == 2)
          color |= 0xECECEF; /* Slight Blue tint */

        /* Subtle darkening for vertical grid effect */
        if (y % 3 == 0) {
          uint8_t r = (color >> 16) & 0xFF;
          uint8_t g = (color >> 8) & 0xFF;
          uint8_t b = color & 0xFF;
          color = 0xFF000000 | ((r * 240 / 255) << 16) |
                  ((g * 240 / 255) << 8) | (b * 240 / 255);
        }

        pixels[y * (pitch / 4) + x] = color;
      }
    }
    SDL_UnlockTexture(crt_mask_texture);
  }
}

static void create_light_map_texture(void) {
  if (renderer == NULL)
    return;
  if (light_map != NULL)
    SDL_DestroyTexture(light_map);
  if (map_mask != NULL)
    SDL_DestroyTexture(map_mask);
  light_map = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, 320, 200);
  SDL_SetTextureBlendMode(light_map, SDL_BLENDMODE_ADD);
  create_map_mask_texture();
}

static void create_light_sprite_texture(void) {
  uint32_t *pixels;
  int pitch;
  int x, y;
  const int size = 64;
  const float center = size / 2.0f;

  if (renderer == NULL)
    return;
  if (light_sprite != NULL)
    SDL_DestroyTexture(light_sprite);

  light_sprite = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, size, size);
  if (light_sprite == NULL)
    return;

  SDL_SetTextureBlendMode(light_sprite, SDL_BLENDMODE_ADD);

  if (SDL_LockTexture(light_sprite, NULL, (void **)&pixels, &pitch) == 0) {
    for (y = 0; y < size; y++) {
      for (x = 0; x < size; x++) {
        float dx = x - center;
        float dy = y - center;
        float dist = sqrtf(dx * dx + dy * dy);
        float alpha = 1.0f - (dist / center);
        if (alpha < 0)
          alpha = 0;

        /* Smooth quadratic falloff */
        uint8_t a = (uint8_t)(alpha * alpha * 255);
        pixels[y * (pitch / 4) + x] =
            (a << 24) | (255 << 16) | (255 << 8) | 255;
      }
    }
    SDL_UnlockTexture(light_sprite);
  }
}

static void create_map_mask_texture(void) {
  if (renderer == NULL)
    return;
  if (map_mask != NULL)
    SDL_DestroyTexture(map_mask);
  map_mask = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_TARGET, 320, 200);
  SDL_SetTextureBlendMode(map_mask, SDL_BLENDMODE_MOD);
  update_map_mask();
}

static void update_map_mask(void) {
  uint32_t *pixels;
  int pitch;
  int x, y;

  if (renderer == NULL || map_mask == NULL)
    return;

  if (SDL_LockTexture(map_mask, NULL, (void **)&pixels, &pitch) == 0) {
    for (y = 0; y < 200; y++) {
      for (x = 0; x < 320; x++) {
        int tile_x = x / (320 / MWIDTH);
        int tile_y = y / (200 / MHEIGHT);
        if (tile_x >= MWIDTH) tile_x = MWIDTH - 1;
        if (tile_y >= MHEIGHT) tile_y = MHEIGHT - 1;
        int tile_idx = tile_y * MWIDTH + tile_x;
        int16_t tile = field[tile_idx];

        uint8_t mask = 0;
        if (tile != -1) {
          uint16_t horiz = tile & 0xfc0;
          uint16_t vert = tile & 0x03f;
          if (horiz == 0 && vert == 0)
            mask = 255;
        }
        pixels[y * (pitch / 4) + x] = (mask << 24) | (255 << 16) | (255 << 8) | 255;
      }
    }
    SDL_UnlockTexture(map_mask);
  }
}

void vgaputi(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {
  SDL_Surface *tmp;
  SDL_Palette *reserv;
  SDL_Rect rect;

  rect.x = virt2scrx(x);
  rect.y = virt2scry(y);
  rect.w = virt2scrw(w);
  rect.h = virt2scrh(h);

  memcpy(&tmp, p, (sizeof(SDL_Surface *)));
  reserv = tmp->format->palette;
  tmp->format->palette = screen16->format->palette;
  SDL_BlitSurface(tmp, NULL, screen16, &rect);
  tmp->format->palette = reserv;
}

void vgageti(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {
  SDL_Surface *tmp;
  SDL_Rect src;

  memcpy(&tmp, p, (sizeof(SDL_Surface *)));
  if (tmp != NULL)
    SDL_FreeSurface(tmp); /* Destroy previously allocated bitmap */

  src.x = virt2scrx(x);
  src.y = virt2scry(y);
  src.w = virt2scrw(w);
  src.h = virt2scrh(h);

  tmp = SDL_CreateRGBSurface(0, src.w, src.h, 8, 0, 0, 0, 0);
  SDL_SetPaletteColors(tmp->format->palette, screen16->format->palette->colors,
                       0, screen16->format->palette->ncolors);
  SDL_BlitSurface(screen16, &src, tmp, NULL);
  memcpy(p, &tmp, (sizeof(SDL_Surface *)));
}

int16_t vgagetpix(int16_t x, int16_t y) {
  SDL_Surface *tmp = NULL;
  uint16_t xi, yi;
  uint16_t i = 0;
  int16_t rval = 0;
  uint8_t *pixels;

  if ((x > 319) || (y > 199))
    return (0xff);

  vgageti(x, y, (uint8_t *)&tmp, 1, 1);
  pixels = (uint8_t *)tmp->pixels;
  for (yi = 0; yi < tmp->h; yi++)
    for (xi = 0; xi < tmp->w; xi++)
      if (pixels[i++])
        rval |= 0x80 >> xi;

  SDL_FreeSurface(tmp);

  return (rval & 0xee);
}

void vgaputim(int16_t x, int16_t y, int16_t ch, int16_t w, int16_t h) {
  SDL_Surface *tmp;
  SDL_Surface *mask;
  SDL_Surface *scr = NULL;
  uint8_t *tmp_pxl, *mask_pxl, *scr_pxl;
  int16_t realsize;
  int16_t i;

  tmp = ch2bmap(&sprites, ch * 2, w, h);
  mask = ch2bmap(&sprites, ch * 2 + 1, w, h);
  vgageti(x, y, (uint8_t *)&scr, w, h);
  realsize = scr->w * scr->h;
  tmp_pxl = (uint8_t *)tmp->pixels;
  mask_pxl = (uint8_t *)mask->pixels;
  scr_pxl = (uint8_t *)scr->pixels;
  for (i = 0; i < realsize; i++)
    if (tmp_pxl[i] != 0xff)
      scr_pxl[i] = (scr_pxl[i] & mask_pxl[i]) | tmp_pxl[i];

  vgaputi(x, y, (uint8_t *)&scr, w, h);
  SDL_FreeSurface(scr);
}

void vgawrite(int16_t x, int16_t y, int16_t ch, int16_t c) {
  SDL_Surface *tmp;
  uint8_t *orig, *copy;
  uint8_t color;
  int16_t w = 3, h = 12, size;
  int16_t i;

  if (!isvalchar(ch))
    return;
  tmp = ch2bmap(&alphas, ch - 32, w, h);
  size = tmp->w * tmp->h;
  copy = (uint8_t *)malloc(size);
  memcpy(copy, tmp->pixels, size);

  for (i = size; i != 0;) {
    i--;
    color = copy[i];
    if (color == 10) {
      if (c == 2)
        color = 12;
      else {
        if (c == 3)
          color = 14;
      }
    } else if (color == 12) {
      if (c == 1)
        color = 2;
      else if (c == 2)
        color = 4;
      else if (c == 3)
        color = 6;
    }
    copy[i] = color;
  }
  orig = (uint8_t *)tmp->pixels;
  tmp->pixels = copy;
  vgaputi(x, y, (uint8_t *)&tmp, w, h);
  tmp->pixels = orig;
  free(copy);
}

void vgatitle(void) {
  SDL_Surface *tmp = NULL;

  vgageti(0, 0, (uint8_t *)&tmp, 80, 200);
  gettitle((uint8_t *)tmp->pixels);
  vgaputi(0, 0, (uint8_t *)&tmp, 80, 200);
  SDL_FreeSurface(tmp);
}

void gretrace(void) {}

void savescreen(void) {
  /*	FILE *f;
          int i;

          f=fopen("screen.saw", "w");

          for(i=0;i<(VGLDisplay->Xsize*VGLDisplay->Ysize);i++)
                  fputc(VGLDisplay->Bitmap[i], f);
          fclose(f);*/
}

void sdl_enable_fullscreen(void) { addflag |= SDL_FULLSCREEN; }

void sdl_toggle_integer_scaling(void) {
  if (renderer == NULL)
    return;
  use_integer_scaling = !use_integer_scaling;
  SDL_RenderSetIntegerScale(renderer,
                            use_integer_scaling ? SDL_TRUE : SDL_FALSE);
}

void sdl_toggle_linear_filter(void) {
  if (renderer == NULL || roottxt == NULL)
    return;
  use_linear_filter = !use_linear_filter;

  /* Recreate texture with new filter setting */
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
              use_linear_filter ? "linear" : "nearest");
  SDL_DestroyTexture(roottxt);
  roottxt = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, 640, 400);
}

int sdl_get_integer_scaling(void) { return use_integer_scaling; }

int sdl_get_linear_filter(void) { return use_linear_filter; }

void sdl_toggle_scanlines(void) {
  use_scanlines = !use_scanlines;
  if (use_scanlines && scanline_overlay == NULL) {
    create_scanline_overlay();
  }
}

void sdl_set_scanline_intensity(int intensity) {
  if (intensity < 0)
    intensity = 0;
  if (intensity > 100)
    intensity = 100;
  scanline_intensity = intensity;
  if (use_scanlines) {
    create_scanline_overlay();
  }
}

int sdl_get_scanlines(void) { return use_scanlines; }

int sdl_get_scanline_intensity(void) { return scanline_intensity; }

#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
void sdl_set_x11_parent(unsigned int xp) { x11_parent = (Window)xp; }
#endif

/*
 * Depreciated functions, necessary only to avoid "Undefined symbol:..."
 * compiler errors.
 */

void sdl_toggle_bloom(void) {
  use_bloom = !use_bloom;
  if (use_bloom && bloom_down == NULL) {
    create_bloom_texture();
  }
}

int sdl_get_bloom(void) { return use_bloom; }

void sdl_toggle_crt_mask(void) {
  use_crt_mask = !use_crt_mask;
  if (use_crt_mask && crt_mask_texture == NULL) {
    create_crt_mask_texture();
  }
}

int sdl_get_crt_mask(void) { return use_crt_mask; }

void sdl_toggle_lighting(void) {
  use_lighting = !use_lighting;
  if (use_lighting) {
    if (light_map == NULL)
      create_light_map_texture();
    if (light_sprite == NULL)
      create_light_sprite_texture();
    if (map_mask == NULL)
      create_map_mask_texture();
  }
}

int sdl_get_lighting(void) { return use_lighting; }

void sdl_toggle_palette_fade(void) { use_palette_fade = !use_palette_fade; }

int sdl_get_palette_fade(void) { return use_palette_fade; }

void sdl_toggle_frame_interp(void) {
  use_frame_interp = !use_frame_interp;
  frame_interp_ready = 0;
}

int sdl_get_frame_interp(void) { return use_frame_interp; }

void sdl_frame_tick_commit(void) {
  SDL_Texture *tmp;

  if (!use_frame_interp)
    return;
  if (renderer == NULL || prev_frame == NULL || curr_frame == NULL)
    return;

  /* Convert screen16 → roottxt (no render/present) */
  flush_screen16_to_roottxt();

  /* Rebuild light map once per tick for interpolated renders */
  rebuild_light_map();

  /* Swap: curr becomes prev */
  tmp = prev_frame;
  prev_frame = curr_frame;
  curr_frame = tmp;

  /* Copy roottxt into new curr_frame */
  SDL_SetRenderTarget(renderer, curr_frame);
  SDL_RenderCopy(renderer, roottxt, NULL, NULL);
  SDL_SetRenderTarget(renderer, NULL);

  if (frame_interp_ready < 2)
    frame_interp_ready++;
}

void doscreenupdate_interp(float t) {
  if (!use_frame_interp || frame_interp_ready < 2 || renderer == NULL ||
      prev_frame == NULL || curr_frame == NULL) {
    doscreenupdate();
    return;
  }

  /* Clamp */
  if (t < 0.0f)
    t = 0.0f;
  if (t > 1.0f)
    t = 1.0f;

  SDL_RenderClear(renderer);

  /* Draw prev_frame at full alpha */
  SDL_SetTextureBlendMode(prev_frame, SDL_BLENDMODE_NONE);
  SDL_SetTextureAlphaMod(prev_frame, 255);
  SDL_RenderCopy(renderer, prev_frame, NULL, NULL);

  /* Draw curr_frame with alpha = t*255 using BLEND mode
   * Result: curr*t + prev*(1-t) */
  SDL_SetTextureBlendMode(curr_frame, SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaMod(curr_frame, (uint8_t)(t * 255.0f));
  SDL_RenderCopy(renderer, curr_frame, NULL, NULL);

  apply_post_effects();
  SDL_RenderPresent(renderer);
}

void sdl_add_light(int x, int y, int r, int g, int b, int radius) {
  SDL_Rect dst;
  if (!use_lighting || renderer == NULL || light_map == NULL ||
      light_sprite == NULL)
    return;

  SDL_SetRenderTarget(renderer, light_map);

  /* Light map is 320x200, but logic coordinates are already 320x200.
   * We should NOT use virt2scrx/y here because that scales to 640x400. */
  dst.w = radius * 2;
  dst.h = radius * 2;
  dst.x = x - dst.w / 2;
  dst.y = y - dst.h / 2;

  SDL_SetTextureColorMod(light_sprite, r, g, b);
  SDL_SetTextureAlphaMod(light_sprite, 120);
  SDL_RenderCopy(renderer, light_sprite, NULL, &dst);

  /* Layer 2: Soft outer glow (wider) */
  dst.w = (int)(dst.w * 1.5f);
  dst.h = (int)(dst.h * 1.5f);
  dst.x = x - dst.w / 2;
  dst.y = y - dst.h / 2;

  SDL_SetTextureAlphaMod(light_sprite, 60);
  SDL_RenderCopy(renderer, light_sprite, NULL, &dst);

  SDL_SetRenderTarget(renderer, NULL);
}

void sdl_save_settings(void) {
  WriteINIBool(INI_GRAPHICS_SETTINGS, "IntegerScale", use_integer_scaling,
               ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "LinearFilter", use_linear_filter,
               ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "Scanlines", use_scanlines, ININAME);
  WriteINIInt(INI_GRAPHICS_SETTINGS, "ScanlineLevel", scanline_intensity,
              ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "Bloom", use_bloom, ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "CRTMask", use_crt_mask, ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "Lighting", use_lighting, ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "PaletteFade", use_palette_fade, ININAME);
  WriteINIBool(INI_GRAPHICS_SETTINGS, "FrameInterp", use_frame_interp, ININAME);
}

void cgainit(void) {}
void cgaclear(void) {}
void cgatitle(void) {}
void cgawrite(int16_t x, int16_t y, int16_t ch, int16_t c) {}
void cgaputim(int16_t x, int16_t y, int16_t ch, int16_t w, int16_t h) {}
void cgageti(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {}
void cgaputi(int16_t x, int16_t y, uint8_t *p, int16_t w, int16_t h) {}
void cgapal(int16_t pal) {}
void cgainten(int16_t inten) {}
int16_t cgagetpix(int16_t x, int16_t y) { return (0); }
