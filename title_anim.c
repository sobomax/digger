#include <string.h>

#include "def.h"
#include "bullet_obj.h"
#include "digger_types.h"
#include "draw_api.h"
#include "drawing.h"
#include "monster_obj.h"
#include "sprite.h"
#include "title_anim.h"

enum title_anim_object_id {
  TITLE_ANIM_OBJ_NONE = -1,
  TITLE_ANIM_OBJ_NOBBIN = 0,
  TITLE_ANIM_OBJ_HOBBIN,
  TITLE_ANIM_OBJ_DIGGER,
  TITLE_ANIM_OBJ_BULLET
};

enum title_anim_action_kind {
  TITLE_ANIM_CLEAR_TEXT = 0,
  TITLE_ANIM_SPAWN_MONSTER,
  TITLE_ANIM_MOVE_MONSTER,
  TITLE_ANIM_TURN_MONSTER,
  TITLE_ANIM_MUTATE_MONSTER,
  TITLE_ANIM_DAMAGE_MONSTER,
  TITLE_ANIM_KILL_MONSTER,
  TITLE_ANIM_SPAWN_DIGGER,
  TITLE_ANIM_MOVE_DIGGER,
  TITLE_ANIM_TURN_DIGGER,
  TITLE_ANIM_DISCHARGE_DIGGER,
  TITLE_ANIM_RECHARGE_DIGGER,
  TITLE_ANIM_FIRE_BULLET,
  TITLE_ANIM_MOVE_BULLET,
  TITLE_ANIM_EXPLODE_BULLET,
  TITLE_ANIM_REMOVE_BULLET,
  TITLE_ANIM_LABEL,
  TITLE_ANIM_DRAW_GOLD,
  TITLE_ANIM_DRAW_EMERALD,
  TITLE_ANIM_DRAW_BONUS
};

struct title_anim_action {
  int16_t start_frame;
  int16_t end_frame;
  enum title_anim_action_kind kind;
  int16_t obj_id;
  int16_t x;
  int16_t y;
  int16_t dx;
  int16_t dy;
  const char *text;
};

#define TITLE_ANIM_ONESHOT(frame, kind, obj_id, x, y, dx, dy, text) \
  {(frame), (frame), (kind), (obj_id), (x), (y), (dx), (dy), (text)}
#define TITLE_ANIM_SPAN(start, end, kind, obj_id, dx, dy) \
  {(start), (end), (kind), (obj_id), 0, 0, (dx), (dy), NULL}

static const struct title_anim_action title_anim_script[] = {
  TITLE_ANIM_ONESHOT(0, TITLE_ANIM_CLEAR_TEXT, TITLE_ANIM_OBJ_NONE,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(50, TITLE_ANIM_SPAWN_MONSTER, TITLE_ANIM_OBJ_NOBBIN,
    292, 63, DIR_LEFT, MON_NOBBIN, NULL),
  TITLE_ANIM_SPAN(51, 77, TITLE_ANIM_MOVE_MONSTER, TITLE_ANIM_OBJ_NOBBIN,
    -4, 0),
  TITLE_ANIM_ONESHOT(78, TITLE_ANIM_TURN_MONSTER, TITLE_ANIM_OBJ_NOBBIN,
    0, 0, DIR_RIGHT, 0, NULL),
  TITLE_ANIM_ONESHOT(83, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 64, 0, 0, "NOBBIN"),
  TITLE_ANIM_ONESHOT(90, TITLE_ANIM_SPAWN_MONSTER, TITLE_ANIM_OBJ_HOBBIN,
    292, 82, DIR_LEFT, MON_NOBBIN, NULL),
  TITLE_ANIM_SPAN(91, 117, TITLE_ANIM_MOVE_MONSTER, TITLE_ANIM_OBJ_HOBBIN,
    -4, 0),
  TITLE_ANIM_ONESHOT(100, TITLE_ANIM_MUTATE_MONSTER, TITLE_ANIM_OBJ_HOBBIN,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(118, TITLE_ANIM_TURN_MONSTER, TITLE_ANIM_OBJ_HOBBIN,
    0, 0, DIR_RIGHT, 0, NULL),
  TITLE_ANIM_ONESHOT(123, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 83, 0, 0, "HOBBIN"),
  TITLE_ANIM_ONESHOT(130, TITLE_ANIM_SPAWN_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    292, 101, DIR_LEFT, 0, NULL),
  TITLE_ANIM_SPAN(131, 157, TITLE_ANIM_MOVE_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    -4, 0),
  TITLE_ANIM_ONESHOT(163, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 102, 0, 0, "DIGGER"),
  TITLE_ANIM_ONESHOT(166, TITLE_ANIM_TURN_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, DIR_RIGHT, 0, NULL),
  TITLE_ANIM_ONESHOT(228, TITLE_ANIM_TURN_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, DIR_UP, 0, NULL),
  TITLE_ANIM_ONESHOT(178, TITLE_ANIM_DRAW_GOLD, TITLE_ANIM_OBJ_NONE,
    184, 120, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(183, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 121, 0, 0, "GOLD"),
  TITLE_ANIM_ONESHOT(198, TITLE_ANIM_DRAW_EMERALD, TITLE_ANIM_OBJ_NONE,
    184, 141, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(203, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 140, 0, 0, "EMERALD"),
  TITLE_ANIM_ONESHOT(218, TITLE_ANIM_DRAW_BONUS, TITLE_ANIM_OBJ_NONE,
    184, 158, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(223, TITLE_ANIM_LABEL, TITLE_ANIM_OBJ_NONE,
    216, 159, 0, 0, "BONUS"),
  TITLE_ANIM_ONESHOT(232, TITLE_ANIM_DISCHARGE_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(232, TITLE_ANIM_FIRE_BULLET, TITLE_ANIM_OBJ_BULLET,
    DIR_UP, 4, 0, 0, NULL),
  TITLE_ANIM_SPAN(233, 237, TITLE_ANIM_MOVE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, -4),
  TITLE_ANIM_ONESHOT(237, TITLE_ANIM_KILL_MONSTER, TITLE_ANIM_OBJ_HOBBIN,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(238, TITLE_ANIM_EXPLODE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(243, TITLE_ANIM_REMOVE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(247, TITLE_ANIM_RECHARGE_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(251, TITLE_ANIM_DISCHARGE_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(251, TITLE_ANIM_FIRE_BULLET, TITLE_ANIM_OBJ_BULLET,
    DIR_UP, 4, 0, 0, NULL),
  TITLE_ANIM_SPAN(252, 260, TITLE_ANIM_MOVE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, -4),
  TITLE_ANIM_ONESHOT(260, TITLE_ANIM_KILL_MONSTER, TITLE_ANIM_OBJ_NOBBIN,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(261, TITLE_ANIM_EXPLODE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(266, TITLE_ANIM_REMOVE_BULLET, TITLE_ANIM_OBJ_BULLET,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(270, TITLE_ANIM_RECHARGE_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, 0, 0, NULL),
  TITLE_ANIM_ONESHOT(274, TITLE_ANIM_TURN_DIGGER, TITLE_ANIM_OBJ_DIGGER,
    0, 0, DIR_RIGHT, 0, NULL)
};

static const int16_t title_anim_cycle_last_frame = 286;

static struct monster_obj *
title_anim_get_monster(struct title_anim *self, int16_t obj_id)
{
  if (obj_id == TITLE_ANIM_OBJ_NOBBIN)
    return (self->nobbin);
  if (obj_id == TITLE_ANIM_OBJ_HOBBIN)
    return (self->hobbin);
  return (NULL);
}

static int16_t *
title_anim_get_spawn_frame(struct title_anim *self, int16_t obj_id)
{
  if (obj_id == TITLE_ANIM_OBJ_NOBBIN)
    return (&self->nobbin_spawn_frame);
  if (obj_id == TITLE_ANIM_OBJ_HOBBIN)
    return (&self->hobbin_spawn_frame);
  if (obj_id == TITLE_ANIM_OBJ_DIGGER)
    return (&self->digger_spawn_frame);
  if (obj_id == TITLE_ANIM_OBJ_BULLET)
    return (&self->bullet_spawn_frame);
  return (NULL);
}

static void
title_anim_clear_text(struct digger_draw_api *ddap)
{
  int16_t y;

  for (y = 54; y < 174; y += 12)
    erasetext(ddap, 12, 164, y, 0);
}

static void
title_anim_put_bullet(struct bullet_obj *bop)
{

  movedrawspr(FIRSTFIREBALL + bop->f_id, bop->x, bop->y);
}

static void
title_anim_animate_bullet(struct bullet_obj *bop)
{

  drawfire(bop->f_id, bop->x, bop->y, bop->expsn);
  if (bop->expsn > 0)
    bop->expsn += 1;
}

static void
title_anim_remove_bullet_sprite(struct bullet_obj *bop)
{

  erasespr(FIRSTFIREBALL + bop->f_id);
  bop->expsn = 0;
}

static void
title_anim_explode_bullet(struct bullet_obj *bop)
{

  bop->expsn = 1;
}

static void
title_anim_remove_bullet(struct title_anim *self)
{
  if (!self->bullet_active)
    return;
  title_anim_remove_bullet_sprite(&self->bullet);
  self->bullet_active = false;
  self->bullet_spawn_frame = -1;
}

static void
title_anim_remove_monster(struct monster_obj **mopp, int16_t sprite_id)
{
  if (*mopp == NULL)
    return;
  erasespr(sprite_id);
  CALL_METHOD(*mopp, dtor);
  *mopp = NULL;
}

static void
title_anim_reset(struct title_anim *self)
{

  title_anim_remove_bullet(self);
  if (self->digger_active) {
    erasespr(FIRSTDIGGER + self->digger.d_id);
    self->digger_active = false;
  }
  title_anim_remove_monster(&self->nobbin, FIRSTMONSTER + 0);
  title_anim_remove_monster(&self->hobbin, FIRSTMONSTER + 1);
  self->nobbin_spawn_frame = -1;
  self->hobbin_spawn_frame = -1;
  self->digger_spawn_frame = -1;
  self->bullet_spawn_frame = -1;
}

static void
title_anim_apply_action(struct title_anim *self, struct digger_draw_api *ddap,
  const struct title_anim_action *ap, int16_t frame)
{
  struct monster_obj *mop;
  struct obj_position pos;
  int16_t *spawn_framep;

  if (frame < ap->start_frame || frame > ap->end_frame)
    return;
  switch (ap->kind) {
    case TITLE_ANIM_CLEAR_TEXT:
      title_anim_clear_text(ddap);
      break;
    case TITLE_ANIM_SPAWN_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop != NULL)
        break;
      mop = monster_obj_ctor((uint16_t)ap->obj_id, ap->dy != 0, ap->dx,
        ap->x, ap->y);
      if (mop == NULL)
        break;
      if (ap->obj_id == TITLE_ANIM_OBJ_NOBBIN)
        self->nobbin = mop;
      else
        self->hobbin = mop;
      CALL_METHOD(mop, put);
      spawn_framep = title_anim_get_spawn_frame(self, ap->obj_id);
      if (spawn_framep != NULL)
        *spawn_framep = frame;
      break;
    case TITLE_ANIM_MOVE_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop == NULL)
        break;
      CALL_METHOD(mop, getpos, &pos);
      pos.x += ap->dx;
      pos.y += ap->dy;
      CALL_METHOD(mop, setpos, &pos);
      break;
    case TITLE_ANIM_TURN_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop == NULL)
        break;
      CALL_METHOD(mop, getpos, &pos);
      pos.dir = ap->dx;
      CALL_METHOD(mop, setpos, &pos);
      break;
    case TITLE_ANIM_MUTATE_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop != NULL)
        CALL_METHOD(mop, mutate);
      break;
    case TITLE_ANIM_DAMAGE_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop != NULL && CALL_METHOD(mop, isalive))
        CALL_METHOD(mop, damage);
      break;
    case TITLE_ANIM_KILL_MONSTER:
      mop = title_anim_get_monster(self, ap->obj_id);
      if (mop != NULL)
        CALL_METHOD(mop, kill);
      break;
    case TITLE_ANIM_SPAWN_DIGGER:
      digger_obj_init(&self->digger, 0, ap->dx, ap->x, ap->y);
      self->digger_active = true;
      self->digger_spawn_frame = frame;
      CALL_METHOD(&self->digger, put);
      break;
    case TITLE_ANIM_MOVE_DIGGER:
      if (!self->digger_active)
        break;
      self->digger.x += ap->dx;
      self->digger.y += ap->dy;
      break;
    case TITLE_ANIM_TURN_DIGGER:
      if (self->digger_active)
        self->digger.dir = ap->dx;
      break;
    case TITLE_ANIM_DISCHARGE_DIGGER:
      if (self->digger_active && self->digger.can_fire)
        CALL_METHOD(&self->digger, discharge);
      break;
    case TITLE_ANIM_RECHARGE_DIGGER:
      if (self->digger_active && !self->digger.can_fire)
        CALL_METHOD(&self->digger, recharge);
      break;
    case TITLE_ANIM_FIRE_BULLET:
      if (!self->digger_active)
        break;
      title_anim_remove_bullet(self);
      bullet_obj_init(&self->bullet, 0, ap->x, self->digger.x + ap->y,
        self->digger.y + ap->dx);
      title_anim_put_bullet(&self->bullet);
      self->bullet_active = true;
      self->bullet_spawn_frame = frame;
      break;
    case TITLE_ANIM_MOVE_BULLET:
      if (!self->bullet_active || self->bullet.expsn != 0)
        break;
      self->bullet.x += ap->dx;
      self->bullet.y += ap->dy;
      break;
    case TITLE_ANIM_EXPLODE_BULLET:
      if (self->bullet_active && self->bullet.expsn == 0)
        title_anim_explode_bullet(&self->bullet);
      break;
    case TITLE_ANIM_REMOVE_BULLET:
      title_anim_remove_bullet(self);
      break;
    case TITLE_ANIM_LABEL:
      outtext(ddap, ap->text, ap->x, ap->y, 2);
      break;
    case TITLE_ANIM_DRAW_GOLD:
      movedrawspr(FIRSTBAG, ap->x, ap->y);
      drawgold(0, 0, ap->x, ap->y);
      break;
    case TITLE_ANIM_DRAW_EMERALD:
      drawemerald(ap->x, ap->y);
      break;
    case TITLE_ANIM_DRAW_BONUS:
      drawbonus(ap->x, ap->y);
      break;
  }
}

static void
title_anim_animate(struct title_anim *self, int16_t frame)
{
  if (self->nobbin != NULL && frame > self->nobbin_spawn_frame)
    CALL_METHOD(self->nobbin, animate);
  if (self->hobbin != NULL && frame > self->hobbin_spawn_frame)
    CALL_METHOD(self->hobbin, animate);
  if (self->digger_active && frame > self->digger_spawn_frame)
    CALL_METHOD(&self->digger, animate);
  if (self->bullet_active && frame > self->bullet_spawn_frame &&
      self->bullet.expsn < 4)
    title_anim_animate_bullet(&self->bullet);
}

void
title_anim_init(struct title_anim *self)
{

  memset(self, '\0', sizeof(*self));
  self->nobbin_spawn_frame = -1;
  self->hobbin_spawn_frame = -1;
  self->digger_spawn_frame = -1;
  self->bullet_spawn_frame = -1;
}

void
title_anim_step(struct title_anim *self, struct digger_draw_api *ddap, int16_t frame)
{
  size_t i;

  if (frame == 0)
    title_anim_reset(self);
  for (i = 0; i < sizeof(title_anim_script) / sizeof(title_anim_script[0]); i++)
    title_anim_apply_action(self, ddap, &title_anim_script[i], frame);
  title_anim_animate(self, frame);
}

void
title_anim_cleanup(struct title_anim *self)
{

  title_anim_reset(self);
}

int16_t
title_anim_last_frame(void)
{

  return (title_anim_cycle_last_frame);
}
