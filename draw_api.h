#define MAX_W 320
#define MAX_H 200
#define CHR_W 12
#define CHR_H 12

struct digger_draw_api {
  void (*ginit)(void);
  void (*gclear)(void);
  void (*gpal)(int16_t pal);
  void (*ginten)(int16_t inten);
  void (*gputi)(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
  void (*ggeti)(int16_t x,int16_t y,uint8_t *p,int16_t w,int16_t h);
  void (*gputim)(int16_t x,int16_t y,int16_t ch,int16_t w,int16_t h);
  int16_t (*ggetpix)(int16_t x,int16_t y);
  void (*gtitle)(void);
  void (*gwrite)(int16_t x,int16_t y,int16_t ch,int16_t c);

  void (*gflush)(void);
};
