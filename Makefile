CC	?= gcc
CFLAGS	+= -O -g -pipe -DDIGGER_DEBUG
RCFLAGS = -D_SDL -std=c99 -Wall
OBJS	= main.o digger.o drawing.o sprite.o scores.o record.o sound.o \
		newsnd.o ini.o input.o monster.o bags.o alpha.o vgagrafx.o \
		title_gz.o icon.o sdl_kbd.o sdl_vid.o sdl_timer.o sdl_snd.o \
		digger_math.o monster_obj.o digger_obj.o bullet_obj.o

ARCH	?= LINUX
#ARCH	?= MINGW
#ARCH	?= FREEBSD
#ARCH	?= FooOS

ifeq ($(ARCH),MINGW)
CC	=  i686-w64-mingw32-gcc
WINDRES	=  i686-w64-mingw32-windres
RCFLAGS	+= -DMINGW -Dmain=SDL_main -I../zlib-1.2.8/include -I../SDL2-2.0.3/include
LIBS	+= -mwindows -lmingw32 -L../SDL2-2.0.3/i686-w64-mingw32/lib -lSDL2main -lSDL2 -luser32 -lgdi32 -lwinmm -L../zlib-1.2.8/lib -lzdll -lm
ESUFFIX	=  .exe
OBJS	+=  digger.res
endif

ifeq ($(ARCH),FREEBSD)
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DFREEBSD $(shell sdl2-config --cflags)
LIBS	+= $(shell sdl2-config --libs) -lz -lm -lX11
ESUFFIX	=
endif

ifeq ($(ARCH),LINUX)
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DLINUX $(shell sdl2-config --cflags)
LIBS	+= $(shell sdl2-config --libs) -lz -lm -lX11
ESUFFIX	=
endif

ifeq ($(ARCH),FooOS)
OBJS	+=		# insert here the names of the files which contains various missing functions like strup() on Linux and FreeBSD
RCFLAGS	+= -DFooOS	# insert here additional compiler flags which required to find include files, trigger os-specific compiler behaviour etc.
LIBS	+= 		# insert here libs required to compile like zlib, SDL etc
ESUFFIX	=		# insert here suffix of the executable on your platform if any (like ".exe" on Win32)
endif

all: digger$(ESUFFIX)

digger$(ESUFFIX): $(OBJS)
	$(CC) -o digger$(ESUFFIX) $(OBJS) $(LIBS)

%.o : %.c
	$(CC) -c $(RCFLAGS) $(CFLAGS) $< -o $@

%.res : %.rc
	$(WINDRES) $< -O coff -o $@

clean:
	rm -f $(OBJS) digger$(ESUFFIX)
