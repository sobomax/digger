/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

extern const uint8_t const *ascii2vga[];

#define isvalchar(ch) ((((ch) - 32) < 0x5f) && ((ch) > 32) && ascii2vga[(ch) - 32] != NULL)
