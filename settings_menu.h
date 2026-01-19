#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H

#include "draw_api.h"

/* Show settings menu before game starts.
 * Returns 1 to start game, 0 to exit */
int show_settings_menu(struct digger_draw_api *ddap);

#endif /* SETTINGS_MENU_H */
