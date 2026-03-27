#ifndef TITLE_ANIM_H
#define TITLE_ANIM_H

#include "def.h"
#include "bullet_obj.h"
#include "digger_obj.h"
#include "monster_obj.h"

struct digger_draw_api;

struct title_anim {
  struct monster_obj *nobbin;
  struct monster_obj *hobbin;
  struct digger_obj digger;
  struct bullet_obj bullet;
  bool digger_active;
  bool bullet_active;
  int16_t nobbin_spawn_frame;
  int16_t hobbin_spawn_frame;
  int16_t digger_spawn_frame;
  int16_t bullet_spawn_frame;
};

void title_anim_init(struct title_anim *self);
void title_anim_step(struct title_anim *self, struct digger_draw_api *ddap,
  int16_t frame);
void title_anim_cleanup(struct title_anim *self);
int16_t title_anim_last_frame(void);

#endif
