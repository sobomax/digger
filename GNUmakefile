CC	?= gcc
CFLAGS	+= -pipe
ifeq (${BUILD_TYPE},production)
CFLAGS  += -O3
else
CFLAGS  += -O0 -g3 -DDIGGER_DEBUG
endif
RCFLAGS = -D_SDL -D_SDL_SOUND -std=gnu11 -Wall -DNO_SND_FILTER #-DNO_SND_EFFECTS
OBJS	= main.o digger.o drawing.o sprite.o scores.o record.o sound.o \
		sound_backend.o \
		newsnd.o ini.o input.o monster.o bags.o alpha.o vgagrafx.o \
	title_gz.o icon.o sdl_kbd.o sdl_vid.o sdl_timer.o sdl_snd.o \
	digger_math.o monster_obj.o digger_obj.o bullet_obj.o title_anim.o \
	cgagrafx.o keyboard.o soundgen.o spinlock.o game.o digger_log.o \
	netsim.o netsim_friends.o netsim_sip.o netsim_sip_registrar.o netsim_sip_sdp.o netsim_platform.o \
	netsim_debug.o
NETSIM_OBJ = netsim.o
MSIP	= microsippy
MSRC	= $(MSIP)/src
MPOSIX	= $(MSIP)/platforms/posix
MWIN	= $(MSIP)/platforms/win32
DIGGER_SIP_COMMON_OBJS = \
	netsim_sip_platform.o \
	$(MSRC)/usipy_sip_tm.o \
	$(MSRC)/usipy_sip_tm_uac.o \
	$(MSRC)/usipy_sip_tm_uas.o \
	$(MSRC)/usipy_sip_tm_dialog.o \
	$(MSRC)/sip_ua/usipy_sip_ua.o \
	$(MSRC)/sip_ua/usipy_sip_ua_idle.o \
	$(MSRC)/sip_ua/usipy_sip_ua_trying.o \
	$(MSRC)/sip_ua/usipy_sip_ua_dialing.o \
	$(MSRC)/sip_ua/usipy_sip_ua_connected.o \
	$(MSRC)/sip_ua/usipy_sip_ua_disconnected.o \
	$(MSRC)/sip_ua/usipy_sip_ua_utils.o \
	$(MSRC)/usipy_sip_msg.o \
	$(MSRC)/usipy_msg_heap.o \
	$(MSRC)/usipy_str.o \
	$(MSRC)/usipy_sip_hdr.o \
	$(MSRC)/usipy_fast_parser.o \
	$(MSRC)/usipy_sip_hdr_db.o \
	$(MSRC)/usipy_sip_sline.o \
	$(MSRC)/usipy_misc.o \
	$(MSRC)/usipy_sip_method_db.o \
	$(MSRC)/usipy_sip_hdr_cseq.o \
	$(MSRC)/usipy_sip_hdr_via.o \
	$(MSRC)/usipy_sip_uri.o \
	$(MSRC)/usipy_sip_req.o \
	$(MSRC)/usipy_sip_hdr_onetoken.o \
	$(MSRC)/usipy_sip_hdr_nameaddr.o \
	$(MSRC)/usipy_sip_hdr_auth.o \
	$(MSRC)/usipy_sip_hdr_authz.o \
	$(MSRC)/usipy_sip_tid.o \
	$(MSRC)/usipy_sip_res.o \
	$(MSRC)/usipy_sip_response_utils.o \
	$(MSRC)/usipy_sip_tm_utils.o \
	$(MSRC)/usipy_sip_dialog.o \
	$(MSRC)/usipy_platform.o \
	$(MSRC)/external/mackron_md5/md5.o \
	$(MPOSIX)/usipy_tm_uac.o
DIGGER_SIP_POSIX_OBJS = \
	$(MPOSIX)/usipy_platform_default.o
DIGGER_SIP_WIN32_OBJS = \
	$(MWIN)/usipy_platform_default.o
DIGGER_SIP_OBJS = $(DIGGER_SIP_COMMON_OBJS)
DIGGER_EXTRA_DEPS =
DIGGER_VERSION_HDR =
WASM_BUILD_INFO =
WASM_DIST_DIR =
WASM_DIST_ARTIFACTS =

ARCH	?= LINUX
#ARCH	?= MINGW
#ARCH	?= FREEBSD
#ARCH	?= WASM
#ARCH	?= FooOS
SDL_VER ?= 2.32.10
ZLIB_VER ?= 1.3.1
MGW_PREF ?= i686-w64-mingw32
MINGW_DEPS_ROOT ?= ../
MGW64_PREF ?= x86_64-w64-mingw32
MINGW_LTO_FLAGS ?= -flto

ifdef CI_COVERAGE
CFLAGS += --coverage
LIBS += --coverage
endif

ifeq ($(ARCH),MINGW)
CC	=  ${MGW_PREF}-gcc
WINDRES	?=  windres
STRIP   ?= strip
CFLAGS	+= $(MINGW_LTO_FLAGS)
LIBS	+= $(MINGW_LTO_FLAGS)
OBJS	+= $(DIGGER_SIP_OBJS)
OBJS	+= $(DIGGER_SIP_WIN32_OBJS)
DIGGER_VERSION_HDR = digger_version.h
RCFLAGS	+= -DMINGW -DUSIPY_BIGENDIAN=0 -Dmain=SDL_main \
	-I./$(MSRC) -I./$(MWIN) -I./$(MWIN)/usipy_port \
	-I./$(MPOSIX) -I./$(MPOSIX)/usipy_port \
	-I${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER} \
	-I${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW_PREF}/include/SDL2
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
CFLAGS	+= $(MINGW_LTO_FLAGS)
LIBS	+= $(MINGW_LTO_FLAGS)
OBJS    += $(DIGGER_SIP_OBJS)
OBJS    += $(DIGGER_SIP_WIN32_OBJS)
DIGGER_VERSION_HDR = digger_version.h
RCFLAGS += -DMINGW -DUSIPY_BIGENDIAN=0 -Dmain=SDL_main \
	-I./$(MSRC) -I./$(MWIN) -I./$(MWIN)/usipy_port \
	-I./$(MPOSIX) -I./$(MPOSIX)/usipy_port \
	-I${MINGW_DEPS_ROOT}/zlib-${ZLIB_VER} \
	-I${MINGW_DEPS_ROOT}/SDL2-${SDL_VER}/${MGW64_PREF}/include/SDL2
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
	OBJS	+= $(DIGGER_SIP_COMMON_OBJS)
	OBJS	+= $(DIGGER_SIP_POSIX_OBJS)
	DIGGER_VERSION_HDR = digger_version.h
	RCFLAGS	+= -DFREEBSD -DUSIPY_BIGENDIAN=0 \
		-I./$(MSRC) -I./$(MPOSIX) \
		-I./$(MPOSIX)/usipy_port \
		$(shell pkg-config sdl2 --cflags)
	LIBS	+= $(shell pkg-config sdl2 --libs) -lz -lm -lX11 -lpthread
	ESUFFIX	=
endif

ifeq ($(ARCH),LINUX)
	OBJS	+= fbsd_sup.o	# strup()
	OBJS	+= $(DIGGER_SIP_COMMON_OBJS)
	OBJS	+= $(DIGGER_SIP_POSIX_OBJS)
	DIGGER_VERSION_HDR = digger_version.h
	RCFLAGS	+= -DLINUX -DUSIPY_BIGENDIAN=0 \
		-I./$(MSRC) -I./$(MPOSIX) \
		-I./$(MPOSIX)/usipy_port \
		$(shell pkg-config sdl2 --cflags)
	LIBS	+= $(shell pkg-config sdl2 --libs) -lz -lm -lX11 -lpthread
	ESUFFIX	=
endif

ifeq ($(ARCH),WASM)
CC      = emcc
CFLAGS  += -flto=full -DUNIX -s USE_SDL=2 -s USE_ZLIB=1
OBJS    := $(filter-out netsim.o,$(OBJS))
OBJS    += netsim_stubs.o
NETSIM_OBJ = netsim_stubs.o
OBJS    += ems_vid.o
OBJS    += fbsd_sup.o
WASM_BUILD_INFO = digger-build-info.js
WASM_DIST_DIR = web-dist
WASM_DIST_ARTIFACTS = digger.html digger.js digger.wasm $(WASM_BUILD_INFO)
DIGGER_EXTRA_DEPS += $(WASM_BUILD_INFO)
RCFLAGS += -DLINUX
LIBS    += -s ASYNCIFY \
	--emrun -lm --shell-file shell.html
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

ifneq ($(DIGGER_VERSION_HDR),)
$(DIGGER_VERSION_HDR):
	sh ./scripts/gen-build-info.sh $@

netsim_sip_platform.o: $(DIGGER_VERSION_HDR)
endif

ifneq ($(WASM_BUILD_INFO),)
$(WASM_BUILD_INFO):
	sh ./scripts/gen-build-info.sh $@
endif

%.res : %.rc
	$(WINDRES) $< -O coff -o $@

clean:
	rm -f $(OBJS) digger$(ESUFFIX) $(WASM_BUILD_INFO) $(DIGGER_VERSION_HDR) *.gcov *.gcda *.gcno
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
