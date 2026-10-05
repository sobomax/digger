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

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>
#include <emscripten/emscripten.h>

#include "def.h"
#include "ems_kbd.h"
#include "input.h"

/*
 * Current key bindings for the landing page, as JSON:
 * {"p1":{"up":..,"down":..,"left":..,"right":..,"fire":..},"p2":{...},
 *  "music":..,"sound":..,"pause":..,"mode":..,"redefine":..,"exit":..,...}
 * Each value is the SDL name of the primary key bound to that action.
 */
static size_t
keymap_put(char *buf, size_t len, size_t off, const char *s, bool esc)
{

	for (; *s != '\0' && off + 2 < len; s++) {
		if (esc && (*s == '"' || *s == '\\'))
			buf[off++] = '\\';
		buf[off++] = *s;
	}
	buf[off] = '\0';
	return (off);
}

/* JSON structure as is */
#define keymap_append(b, l, o, s) keymap_put((b), (l), (o), (s), false)

static size_t
keymap_entry(char *buf, size_t len, size_t off, const char *name, int key,
  bool first)
{
	const char *kname;

	kname = SDL_GetScancodeName((SDL_Scancode)key);
	if (kname == NULL || *kname == '\0')
		kname = "?";
	off = keymap_append(buf, len, off, first ? "\"" : ",\"");
	off = keymap_append(buf, len, off, name);
	off = keymap_append(buf, len, off, "\":\"");
	off = keymap_put(buf, len, off, kname, true);
	return (keymap_append(buf, len, off, "\""));
}

static size_t
keymap_player(char *buf, size_t len, size_t off, const char *name, int base)
{
	static const char *const acts[] = {"right", "up", "left", "down", "fire"};
	int i;

	off = keymap_append(buf, len, off, "\"");
	off = keymap_append(buf, len, off, name);
	off = keymap_append(buf, len, off, "\":{");
	for (i = 0; i < 5; i++)
		off = keymap_entry(buf, len, off, acts[i],
		  keycodes[base + i][0], i == 0);
	return (keymap_append(buf, len, off, "},"));
}

EMSCRIPTEN_KEEPALIVE const char *
digger_get_keymap(void)
{
	static char buf[1024];
	size_t off;

	off = keymap_append(buf, sizeof(buf), 0, "{");
	off = keymap_player(buf, sizeof(buf), off, "p1", PKEY_RIGHT);
	off = keymap_player(buf, sizeof(buf), off, "p2", PKEY_RIGHT2);
	off = keymap_entry(buf, sizeof(buf), off, "accel",
	  keycodes[DKEY_SUP][0], true);
	off = keymap_entry(buf, sizeof(buf), off, "brake",
	  keycodes[DKEY_SDN][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "music",
	  keycodes[DKEY_MTG][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "sound",
	  keycodes[DKEY_STG][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "exit",
	  keycodes[DKEY_EXT][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "pause",
	  keycodes[DKEY_PUS][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "mode",
	  keycodes[DKEY_MCH][0], false);
	off = keymap_entry(buf, sizeof(buf), off, "redefine",
	  keycodes[DKEY_RDK][0], false);
	(void)keymap_append(buf, sizeof(buf), off, "}");
	return (buf);
}

/* Tell the landing page that the key bindings have (possibly) changed */
void
ems_keymap_changed(void)
{

	EM_ASM({
		if (typeof window !== 'undefined' &&
		    typeof window.diggerKeymapChanged === 'function')
			window.diggerKeymapChanged();
	});
}
