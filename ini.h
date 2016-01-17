/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include "def.h"

void WriteINIString(char *section,char *key,char *value,char *filename);
void GetINIString(char *section,char *key,char *def,char *dest,int destsize,
                  char *filename);
int32_t GetINIInt(char *section,char *key,int32_t def,char *filename);
void WriteINIInt(char *section,char *key,int32_t value,char *filename);
bool GetINIBool(char *section,char *key,bool def,char *filename);
void WriteINIBool(char *section,char *key,bool value,char *filename);
