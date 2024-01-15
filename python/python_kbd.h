#pragma once

struct digger_controls {
    bool leftpressed;
    bool rightpressed;
    bool uppressed;
    bool downpressed;
    bool f1pressed;
    bool left2pressed;
    bool right2pressed;
    bool up2pressed;
    bool down2pressed;
    bool f12pressed;
};

extern struct digger_controls digger_controls;

#define leftpressed digger_controls.leftpressed
#define rightpressed digger_controls.rightpressed
#define uppressed digger_controls.uppressed
#define downpressed digger_controls.downpressed
#define f1pressed digger_controls.f1pressed
#define left2pressed digger_controls.left2pressed
#define right2pressed digger_controls.right2pressed
#define up2pressed digger_controls.up2pressed
#define down2pressed digger_controls.down2pressed
#define f12pressed digger_controls.f12pressed