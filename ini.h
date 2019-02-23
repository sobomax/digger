/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include "def.h"

void WriteINIString(const char *section,const char *key,const char *value,const char *filename);
void GetINIString(const char *section,const char *key,const char*def,char*dest,int destsize,
                  char *filename);

#define GetINIIntDoc(s,k,de,f,do) GetINIInt((s),(k),(de),(f))

int32_t GetINIInt(const char *section,const char*key,int32_t def,const char*filename);
void WriteINIInt(const char *section,const char*key,int32_t value,const char*filename);
bool GetINIBool(const char *section,const char*key,bool def,const char*filename);
void WriteINIBool(char *section,const char*key,bool value,const char*filename);
