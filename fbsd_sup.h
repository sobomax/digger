#include <stdio.h>

#if !defined(__EMSCRIPTEN__)
void strupr(char *);
#endif
void catcher(int);
#ifdef DIGGER_DEBUG
#define FIXME(args) fprintf(stderr, "%s\n", args)
#else
#define FIXME(args) do { (void)(args); } while (0)
#endif

