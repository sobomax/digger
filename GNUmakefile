CC	?= gcc
CFLAGS	+= -pipe
ifeq (${BUILD_TYPE},production)
CFLAGS  += -O3
else
CFLAGS  += -O0 -g3 -DDIGGER_DEBUG
endif
RCFLAGS = -D_SDL -std=c11 -Wall -DNO_SND_FILTER #-DNO_SND_EFFECTS
OBJS	= main.o digger.o drawing.o sprite.o scores.o record.o sound.o \
		newsnd.o ini.o input.o monster.o bags.o alpha.o vgagrafx.o \
		title_gz.o icon.o sdl_kbd.o sdl_vid.o sdl_timer.o sdl_snd.o \
		digger_math.o monster_obj.o digger_obj.o bullet_obj.o \
		cgagrafx.o keyboard.o soundgen.o spinlock.o digger_log.o game.o netsim.o \
		netsim_platform.o \
		netsim_debug.o
DIGGER_EXTRA_DEPS =
WASM_BUILD_INFO =
WASM_DIST_DIR =
WASM_DIST_ARTIFACTS =

ARCH	?= LINUX
#ARCH	?= MINGW
#ARCH	?= FREEBSD
#ARCH	?= WASM
#ARCH	?= FooOS
SDL_VER ?= 2.32.10
ZLIB_VER ?= 1.2.11
MGW_PREF ?= i686-w64-mingw32
MINGW_DEPS_ROOT ?= ../
MGW64_PREF ?= x86_64-w64-mingw32

ifdef CI_COVERAGE
CFLAGS += --coverage
LIBS += --coverage
endif

ifeq ($(ARCH),MINGW)
CC	=  ${MGW_PREF}-gcc
WINDRES	?=  windres
STRIP   ?= strip
RCFLAGS	+= -DMINGW -Dmain=SDL_main -I${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER} -I${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW_PREF}/include/SDL2
LIBS	+= -mwindows -lmingw32 -L${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW_PREF}/lib -lSDL2main -lSDL2 -luser32 -lgdi32 -lwinmm -lws2_32 -L${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER} -lz -lm
ifeq (${BUILD_TYPE},debug)
LIBS    += -mconsole
endif
ESUFFIX	=  .exe
OBJS	+=  digger.res
VPATH   += ./pkg/windows
endif

ifeq ($(ARCH),MINGW64)
CC      =  ${MGW64_PREF}-gcc
WINDRES ?=  windres
STRIP   ?=  strip
RCFLAGS += -DMINGW -Dmain=SDL_main -I${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER} -I${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW64_PREF}/include/SDL2
LIBS    += -mwindows -lmingw32 -L${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW64_PREF}/lib -lSDL2main -lSDL2 -luser32 -lgdi32 -lwinmm -lws2_32 \
            -L${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER}/${MGW64_PREF} -lz -lm
ifeq (${BUILD_TYPE},debug)
LIBS    += -mconsole
endif
ESUFFIX =  .exe
OBJS    +=  digger.res
VPATH   += ./pkg/windows
endif

ifeq ($(ARCH),FREEBSD)
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DFREEBSD $(shell pkg-config sdl2 --cflags)
LIBS	+= $(shell pkg-config sdl2 --libs) -lz -lm -lX11 -lpthread
ESUFFIX	=
endif

ifeq ($(ARCH),LINUX)
OBJS	+= fbsd_sup.o	# strup()
RCFLAGS	+= -DLINUX $(shell pkg-config sdl2 --cflags)
LIBS	+= $(shell pkg-config sdl2 --libs) -lz -lm -lX11 -lpthread
ESUFFIX	=
endif

ifeq ($(ARCH),WASM)
CC      = emcc
CFLAGS  += -flto=full -DUNIX -s USE_SDL=2 -s USE_ZLIB=1 -s ASYNCIFY
OBJS    += ems_vid.o
OBJS    += fbsd_sup.o
WASM_BUILD_INFO = digger-build-info.js
WASM_DIST_DIR = web-dist
WASM_DIST_ARTIFACTS = digger.html digger.js digger.wasm $(WASM_BUILD_INFO)
DIGGER_EXTRA_DEPS += $(WASM_BUILD_INFO)
RCFLAGS += -DLINUX
LIBS    += --emrun -lm --shell-file shell.html
ESUFFIX = .html
SSUFFIX = .wasm
STRIP   = llvm-strip
endif

ifeq ($(ARCH),FooOS)
OBJS	+=		# insert here the names of the files which contains various missing functions like strup() on Linux and FreeBSD
RCFLAGS	+= -DFooOS	# insert here additional compiler flags which required to find include files, trigger os-specific compiler behaviour etc.
LIBS	+= 		# insert here libs required to compile like zlib, SDL etc
ESUFFIX	=		# insert here suffix of the executable on your platform if any (like ".exe" on Win32)
endif

STRIP   ?= strip
SSUFFIX ?= ${ESUFFIX}

all: digger$(ESUFFIX)

digger$(ESUFFIX): $(OBJS) $(DIGGER_EXTRA_DEPS)
	$(CC) $(CFLAGS) -o digger$(ESUFFIX) $(OBJS) $(LIBS)
ifeq (${BUILD_TYPE},production)
	$(STRIP) --strip-unneeded digger$(SSUFFIX)
endif
ifneq ($(WASM_DIST_DIR),)
	mkdir -p $(WASM_DIST_DIR)
	cp $(WASM_DIST_ARTIFACTS) $(WASM_DIST_DIR)/
endif

%.o : %.c
	$(CC) -c $(RCFLAGS) $(CFLAGS) $< -o $@

ifneq ($(WASM_BUILD_INFO),)
$(WASM_BUILD_INFO):
	sh ./scripts/gen-wasm-build-info.sh $@
endif

%.res : %.rc
	$(WINDRES) $< -O coff -o $@

clean:
	rm -f $(OBJS) digger$(ESUFFIX) $(WASM_BUILD_INFO) *.gcov *.gcda *.gcno
	rm -rf $(WASM_DIST_DIR)

ifdef TEST_TYPE
TT_VAR=	TEST_TYPES=$(TEST_TYPE)
else
TT_VAR=
endif

do-test:
	sh -x ./scripts/do-test-cmmn.sh
	SDL_VER=${SDL_VER} ZLIB_VER=${ZLIB_VER} MGW_PREF="${MGW_PREF}" \
	  MGW64_PREF="${MGW64_PREF}" sh -x ./scripts/do-test.sh
	env ${TT_VAR} sh -x ./scripts/do-test-run.sh

do-test-cmake:
	sh -x ./scripts/do-test-cmmn.sh
	sh -x ./scripts/do-test-cmake.sh
	env ${TT_VAR} sh -x ./scripts/do-test-run.sh

coverage-report:
	for s in $(OBJS); \
	do \
	  gcov "$${s%.o}.c"; \
	done

coverage-report-html:
	rm -rf ./digger_lcov
	lcov --directory . --capture --output-file digger_lcov/digger.info
	genhtml -o digger_lcov digger_lcov/digger.info
