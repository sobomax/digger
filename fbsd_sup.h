#include <stdio.h>

#if !defined(__EMSCRIPTEN__)
void strupr(char *);
#endif
void catcher(int);
#define FIXME(args) fprintf(stderr, "%s\n", args)

