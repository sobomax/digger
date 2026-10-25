/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "def.h"
#include "digger.h"
#include "digger_math.h"
#include "draw_api.h"
#include "drawing.h"
#include "edrf_feed.h"
#include "input.h"
#include "netsim.h"
#include "netsim_dialog.h"
#include "sound.h"
#include "sprite.h"

/*
 * The dialog: a frame in the gold bags' brown with a line under its
 * heading, over a shadow in the level 4 stripes, a black line in between.
 * Whom the call's with under the heading, both Diggers facing each other
 * under that (Digger 1 the caller's), what our player can do at the
 * bottom, the choice marked with an emerald.
 */
#define DLG_LEFT 20
#define DLG_RIGHT 300 /* The frame's last 4 pixels up to it */
#define DLG_TOP 59
#define DLG_BOTTOM 145 /* The frame's last 2 rows up to it */
#define DLG_SHADOW_DX 8
#define DLG_SHADOW_DY 4
#define DLG_HEAD_Y 64
#define DLG_HEAD_LINE_Y 79
#define DLG_PEER_Y 88
#define DLG_DIGGERS_Y 104
#define DLG_CHOICE_Y 124
#define DLG_PEER_MAXLEN 16

/* Its Diggers, a sprite each: where, the first of their 3 frames (see
   drawdigger()) */
static const struct {
  int16_t x;
  int16_t ch;
} dlg_diggers[DIGGERS] = {
  {112, 1}, /* Digger 1 facing right */
  {192, 44} /* Digger 2 facing left */
};

/* Back and forth over those 3 frames, as Diggers dig */
static const int16_t dlg_digger_frames[] = {0, 1, 2, 1};
#define DLG_DIGGER_NFRAMES \
  ((int)(sizeof(dlg_digger_frames) / sizeof(dlg_digger_frames[0])))

/* What our player can do: take the peer's call or not, give up on ours */
static const struct {
  const char *text;
  int16_t x;
} dlg_choices[2][2] = {
  {{"HANG UP", 118}, {NULL, 0}},
  {{"ACCEPT", 68}, {"REJECT", 180}}
};

static void
dlg_draw_frame(struct digger_draw_api *ddap)
{
  /* Blocks of solid colour, 4 pixels by a row, for lines of: the gold bags'
     brown, off one drawn where the dialog's to be cleared anyway; black, off
     the dialog cleared */
  static uint8_t brown[1 * 1 * 8], black[1 * 1 * 8];
  static const int16_t hlines[] = {DLG_TOP, DLG_HEAD_LINE_Y, DLG_BOTTOM - 2};
  int16_t x, y;
  size_t i;

  ddap->gputim(140, 100, 62, 4, 15);
  ddap->ggeti(146, 101, brown, 1, 1);
  for (y = DLG_TOP + DLG_SHADOW_DY; y < DLG_BOTTOM + 6; y += 4)
    for (x = DLG_LEFT + DLG_SHADOW_DX; x < DLG_RIGHT + DLG_SHADOW_DX; x += 20)
      ddap->gputim(x, y, 96, 5, 4);
  for (y = DLG_TOP - 1; y < DLG_BOTTOM + 1; y += 12) {
    int16_t ty = (y + 12 > DLG_BOTTOM) ? DLG_BOTTOM - 12 : y;

    erasetext(ddap, (DLG_RIGHT - DLG_LEFT) / 12, DLG_LEFT, ty, 3);
    erasetext(ddap, 1, DLG_RIGHT - 12, ty, 3);
  }
  ddap->ggeti(100, 70, black, 1, 1);
  for (y = DLG_TOP + DLG_SHADOW_DY; y <= DLG_BOTTOM; y++)
    ddap->gputi(DLG_RIGHT - 3, y, black, 1, 1);
  for (x = DLG_LEFT + DLG_SHADOW_DX; x < DLG_RIGHT; x += 4)
    ddap->gputi(x, DLG_BOTTOM, black, 1, 1);
  for (i = 0; i < sizeof(hlines) / sizeof(hlines[0]); i++)
    for (x = DLG_LEFT; x < DLG_RIGHT; x += 4) {
      ddap->gputi(x, hlines[i], brown, 1, 1);
      ddap->gputi(x, hlines[i] + 1, brown, 1, 1);
    }
  for (y = DLG_TOP; y < DLG_BOTTOM; y++) {
    ddap->gputi(DLG_LEFT, y, brown, 1, 1);
    ddap->gputi(DLG_RIGHT - 4, y, brown, 1, 1);
  }
}

static void
dlg_draw_choices(struct digger_draw_api *ddap, bool incoming, int sel)
{
  int i;

  for (i = 0; i < 2; i++) {
    if (dlg_choices[incoming][i].text == NULL)
      continue;
    erasetext(ddap, 2, dlg_choices[incoming][i].x - 24, DLG_CHOICE_Y, 3);
    outtext(ddap, dlg_choices[incoming][i].text, dlg_choices[incoming][i].x,
      DLG_CHOICE_Y, i == sel ? 3 : 1);
  }
  ddap->gputim(dlg_choices[incoming][sel].x - 20, DLG_CHOICE_Y + 1, 108, 4, 10);
}

static void
dlg_draw(struct digger_draw_api *ddap, bool incoming, int sel)
{
  const char *head;
  char peer[DLG_PEER_MAXLEN + 1];
  int i;

  dlg_draw_frame(ddap);
  head = incoming ? "GAME REQUEST" : "GAME REQUESTED";
  outtext(ddap, head, 160 - 6 * (int16_t)strlen(head), DLG_HEAD_Y, 3);
  netsim_call_peer(peer, sizeof(peer));
  outtext(ddap, peer, 160 - 6 * (int16_t)strlen(peer), DLG_PEER_Y, 2);
  outtext(ddap, "VS", 148, DLG_DIGGERS_Y + 2, 1);
  for (i = 0; i < DIGGERS; i++) {
    initspr(FIRSTDIGGER + i, dlg_diggers[i].ch + 1, 4, 15, 0, 0);
    movedrawspr(FIRSTDIGGER + i, dlg_diggers[i].x, DLG_DIGGERS_Y);
  }
  dlg_draw_choices(ddap, incoming, sel);
}

/* Both Diggers digging away, each on a frame of its own (offs), while the
   call's put to the peer's player, or ours */
static void
dlg_animate(int tick, const int *offs)
{
  int i, n;

  for (i = 0; i < DIGGERS; i++) {
    n = dlg_digger_frames[(tick + offs[i]) % DLG_DIGGER_NFRAMES];
    initspr(FIRSTDIGGER + i, dlg_diggers[i].ch + n, 4, 15, 0, 0);
    drawspr(FIRSTDIGGER + i, dlg_diggers[i].x, DLG_DIGGERS_Y);
  }
}

static void
dlg_close(bool incoming)
{
  int i;

  for (i = DIGGERS - 1; i >= 0; i--)
    erasespr(FIRSTDIGGER + i);
  if (incoming)
    soundbonusoff();
  input_set_leftright_start(true);
  /* Not for the title, which they'd be for otherwise */
  mode_change = false;
  keyredef = false;
}

/*
 * The call, the peer's (incoming) or ours, the dialog up for it on the title
 * screen until it's taken or not: the peer's told (180) it's being put to
 * our player, who has the alarm of the bonus running out to call them, and
 * may take it or not; ours goes on until the peer's has, or ours gives up on
 * it. Both Diggers dig away while it's being put to either (the peer's
 * having told us, for ours).
 */
enum netsim_dialog_result
netsim_dialog_run(struct digger_draw_api *ddap, bool incoming)
{
  enum netsim_dialog_result rval;
  enum netsim_call_state cstate;
  bool left_was, right_was, left_now, right_now;
  uint64_t seed;
  int offs[DIGGERS];
  int sel, tick;

  if (!incoming && !netsim_call_start())
    return (NETSIM_DIALOG_FAILED);
  /* Not one frame the same for both, whichever they start on */
  seed = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 32);
  offs[0] = (int)(splitmix64(seed, 0) % DLG_DIGGER_NFRAMES);
  offs[1] = (offs[0] + 1 + (int)(splitmix64(seed, 1) %
    (DLG_DIGGER_NFRAMES - 1))) % DLG_DIGGER_NFRAMES;
  sel = 0;
  dlg_draw(ddap, incoming, sel);
  if (incoming) {
    netsim_call_ring();
    soundwakeup();
    soundbonus();
  }
  input_set_leftright_start(false);
  (void)teststart();
  left_was = right_was = true;
  for (tick = 0;; tick++) {
    cstate = netsim_call_poll();
    if (cstate == NETSIM_CALL_GONE) {
      rval = NETSIM_DIALOG_FAILED;
      break;
    }
    if (!incoming && cstate == NETSIM_CALL_ANSWERED) {
      rval = NETSIM_DIALOG_GO;
      break;
    }
    /* A recording fed to the game: nobody to ask */
    if (incoming && edrf_feeding) {
      rval = NETSIM_DIALOG_GO;
      break;
    }
    if (incoming || cstate == NETSIM_CALL_RINGING)
      dlg_animate(tick, offs);
    left_now = leftpressed;
    right_now = rightpressed;
    if (incoming && ((left_now && !left_was) || (right_now && !right_was))) {
      sel = 1 - sel;
      dlg_draw_choices(ddap, incoming, sel);
    }
    left_was = left_now;
    right_was = right_now;
    if (escape) {
      escape = false;
      rval = NETSIM_DIALOG_DECLINED;
      break;
    }
    if (teststart()) {
      rval = (incoming && sel == 0) ? NETSIM_DIALOG_GO : NETSIM_DIALOG_DECLINED;
      break;
    }
    newframe();
  }
  dlg_close(incoming);
  if (rval == NETSIM_DIALOG_DECLINED && incoming)
    netsim_call_decline();
  else if (rval != NETSIM_DIALOG_GO)
    netsim_call_hangup();
  return (rval);
}
