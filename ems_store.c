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

/*
 * The web build keeps its files (settings, high scores) in Emscripten's
 * in-memory file system, which is gone once the page is reloaded. Mirror
 * them into the browser's localStorage: restore a file from there before it
 * is first read and save it back after every write.
 */

#include <emscripten/emscripten.h>

#include "ems_store.h"

/* Copy localStorage["digger:<path>"] (base64) into the file, if present */
void
ems_store_restore(const char *path)
{

	EM_ASM({
		var path = UTF8ToString($0);
		try {
			var data = window.localStorage.getItem('digger:' + path);
			if (data === null)
				return;
			var bin = atob(data);
			var bytes = new Uint8Array(bin.length);
			for (var i = 0; i < bin.length; i++)
				bytes[i] = bin.charCodeAt(i);
			FS.writeFile(path, bytes);
		} catch (e) {
			console.warn('Cannot restore ' + path + ' from localStorage: ' + e);
		}
	}, path);
}

/* Copy the file into localStorage["digger:<path>"] as base64 */
void
ems_store_save(const char *path)
{

	EM_ASM({
		var path = UTF8ToString($0);
		try {
			var bytes = FS.readFile(path);
			var bin = '';
			for (var i = 0; i < bytes.length; i++)
				bin += String.fromCharCode(bytes[i]);
			window.localStorage.setItem('digger:' + path, btoa(bin));
		} catch (e) {
			console.warn('Cannot save ' + path + ' to localStorage: ' + e);
		}
	}, path);
}
