CC	?= gcc
CFLAGS	+= -O -g -pipe -DDIGGER_DEBUG
RCFLAGS = -D_SDL -std=c99 -Wall
OBJS	= main.o digger.o drawing.o sprite.o scores.o record.o sound.o \
		newsnd.o ini.o input.o monster.o bags.o alpha.o vgagrafx.o \
		title_gz.o icon.o sdl_kbd.o sdl_vid.o sdl_timer.o sdl_snd.o \
		digger_math.o monster_obj.o digger_obj.o bullet_obj.o

ARCH	= "LINUX"
#ARCH	= "MINGW"
#ARCH	= "FREEBSD"
#ARCH	= "FooOS"

ifeq ($(ARCH),"MINGW")
RCFLAGS	+= -mno-cygwin -DMINGW -Dmain=SDL_main -I../zlib -I../SDL-1.1.2/include/SDL
LIBS	+= -mno-cygwin -mwindows -lmingw32 -L../SDL-1.1.2/lib -lSDLmain -lSDL -luser32 -lgdi32 -lwinmm -L../zlib -lz -lm
ESUFFIX	=  .exe
endif

ifeq ($(ARCH),"FREEBSD")
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DFREEBSD $(shell sdl2-config --cflags)
LIBS	+= $(shell sdl2-config --libs) -lz -lm -lX11
ESUFFIX	=
endif

ifeq ($(ARCH),"LINUX")
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DLINUX $(shell sdl2-config --cflags)
LIBS	+= $(shell sdl2-config --libs) -lz -lm -lX11
ESUFFIX	=
endif

ifeq ($(ARCH),"FooOS")
OBJS	+=		# insert here the names of the files which contains various missing functions like strup() on Linux and FreeBSD
RCFLAGS	+= -DFooOS	# insert here additional compiler flags which required to find include files, trigger os-specific compiler behaviour etc.
LIBS	+= 		# insert here libs required to compile like zlib, SDL etc
ESUFFIX	=		# insert here suffix of the executable on your platform if any (like ".exe" on Win32)
endif

all: digger$(ESUFFIX)

digger$(ESUFFIX): $(OBJS)
	$(CC) -o digger$(ESUFFIX) $(OBJS) $(LIBS)

$(OBJS): %.o: %.c
	$(CC) -c $(RCFLAGS) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) digger$(ESUFFIX)
