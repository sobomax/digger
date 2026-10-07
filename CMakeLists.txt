cmake_minimum_required( VERSION 3.10...3.31 )
PROJECT(DIGGER)
SET( VERSION "20110912" )
SET( PACKAGE "digger" )
SET( CMAKE_MODULE_PATH ${DIGGER_SOURCE_DIR}/cmake/modules )
SET( CMAKE_C_STANDARD 11 )
SET( CMAKE_C_STANDARD_REQUIRED ON )
SET( CMAKE_C_EXTENSIONS ON )

IF( NOT MSVC )
  SET( WARN_FLAGS "-Wall -Wformat=2" )
  IF( CMAKE_C_COMPILER_ID MATCHES "Clang" )
    # -pedantic, but for the GNU (and newer C) extensions the code relies on,
    # those of the switches the compiler has (checked as -W..., as -Wno-...
    # is taken whether known or not)
    SET( WARN_FLAGS_DEBUG "-Wextra -pedantic" )
    INCLUDE( CheckCCompilerFlag )
    FOREACH( WEXT gnu zero-length-array c23-extensions variadic-macros
      language-extension-token )
      STRING( MAKE_C_IDENTIFIER "HAVE_W_${WEXT}" WEXT_VAR )
      CHECK_C_COMPILER_FLAG( "-W${WEXT}" ${WEXT_VAR} )
      IF( ${WEXT_VAR} )
        SET( WARN_FLAGS_DEBUG "${WARN_FLAGS_DEBUG} -Wno-${WEXT}" )
      ENDIF()
    ENDFOREACH()
  ELSE()
    # GCC's -pedantic has no switches for the extensions: no -pedantic
    SET( WARN_FLAGS_DEBUG "-Wextra" )
  ENDIF()
  SET( C_FLAGS "-DLINUX -D_REENTRANT -pipe -Wall" )
  SET( CMAKE_C_FLAGS "${C_FLAGS} ${WARN_FLAGS} ${CMAKE_C_FLAGS}" )
  SET( CMAKE_C_FLAGS_DEBUG "${CFLAGS} ${WARN_FLAGS} ${WARN_FLAGS_DEBUG} -g3 -O0 ${CMAKE_C_FLAGS_DEBUG} -DDIGGER_DEBUG" )
ENDIF( NOT MSVC )

if(DEFINED ENV{CI_UBSAN})
  SET(UBSAN_FLAGS "-fsanitize=undefined -fno-sanitize-recover=all")
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${UBSAN_FLAGS}")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${UBSAN_FLAGS}")
endif()

if(DEFINED ENV{CI_COVERAGE})
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage")
  SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} --coverage")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

SET( CPACK_PACKAGE_VERSION, "${VERSION}" )

SET( CPACK_SOURCE_IGNORE_FILES ".git" "build" ".swp$" ".*~" )

SET( CPACK_PACKAGE_NAME, "${PACKAGE}" )
SET( CPACK_SOURCE_GENERATOR "TGZ;TXZ")
SET( CPACK_SOURCE_PACKAGE_FILE_NAME "${PACKAGE}-${VERSION}" )

INCLUDE(CPack)
INCLUDE(GNUInstallDirs)
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCXXSourceCompiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckSymbolExists)

function(check_working_cxx_atomics varname)
  message(CHECK_START "Checking Atomics...")
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++11")
  CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
std::atomic<int> x;
int main() {
  return x;
}
" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction(check_working_cxx_atomics)

# The C code reads and assigns _Atomic bool variables directly, which on
# some targets (e.g. ARMv5) compiles into generic __atomic_load/__atomic_store
# calls from libatomic even when the int-sized C++ atomics above link fine.
function(check_working_c_atomics varname)
  CHECK_C_SOURCE_COMPILES("
#include <stdatomic.h>
#include <stdbool.h>
_Atomic bool b, c;
_Atomic int i;
int main(void) {
  b = c = true;
  atomic_fetch_add(&i, 1);
  return b ? atomic_load(&i) : 0;
}
" ${varname})
endfunction(check_working_c_atomics)

IF( NOT MSVC )
  check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
  check_working_c_atomics(HAVE_C_ATOMICS_WITHOUT_LIB)
  IF (NOT HAVE_CXX_ATOMICS_WITHOUT_LIB OR NOT HAVE_C_ATOMICS_WITHOUT_LIB)
    SET(NEED_LIBATOMIC TRUE)
  ENDIF()
  find_package(Threads REQUIRED)
  CHECK_SYMBOL_EXISTS(clock_nanosleep "time.h" HAVE_CLOCK_NANOSLEEP)
  CHECK_SYMBOL_EXISTS(TIMER_ABSTIME "time.h" HAVE_TIMER_ABSTIME)
  CHECK_SYMBOL_EXISTS(pthread_condattr_setclock "pthread.h" HAVE_PTHREAD_CONDATTR_SETCLOCK)

  IF (NEED_LIBATOMIC)
    check_library_exists(atomic __atomic_fetch_add_4 "" HAVE_LIBATOMIC)

    IF (NOT HAVE_LIBATOMIC)
      message(SEND_ERROR "Unable to configure, missing c11 atomics")
    ENDIF()
  ENDIF()
ENDIF()

IF( NOT DEFINED CMAKE_INSTALL_PREFIX )
  SET( CMAKE_INSTALL_PREFIX /usr/local )
ENDIF( NOT DEFINED CMAKE_INSTALL_PREFIX )

find_package( ZLIB REQUIRED )
IF ( NOT ZLIB_FOUND )
  MESSAGE( FATAL_ERROR " zlib-devel not found" )
ELSE ( NOT ZLIB_FOUND )
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})
ENDIF ( NOT ZLIB_FOUND )

OPTION( DIGGER_VGL
  "Build FreeBSD VGL."
  OFF )
IF ( NOT DIGGER_VGL )
  find_package( SDL2 REQUIRED )
  find_package( X11 )
  IF ( NOT SDL2_FOUND )
    MESSAGE( FATAL_ERROR " SDL2-devel not found" )
  ELSE ( NOT SDL2_FOUND )
    execute_process(COMMAND sdl2-config --cflags RESULT_VARIABLE CMD_RES OUTPUT_VARIABLE SDL2_CFLAGS ERROR_VARIABLE ERR_VAR OUTPUT_STRIP_TRAILING_WHITESPACE)
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SDL2_CFLAGS} -D_SDL -D_SDL_SOUND" )
    INCLUDE_DIRECTORIES(${SDL2_INCLUDE_DIR})
    IF (X11_FOUND)
      SET(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
      SET(OLD_CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES})
      SET(OLD_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
      SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${SDL2_CFLAGS}")
      SET(CMAKE_REQUIRED_INCLUDES ${SDL2_INCLUDE_DIR} ${X11_INCLUDE_DIR})
      SET(CMAKE_REQUIRED_LIBRARIES ${X11_LIBRARIES})
      CHECK_C_SOURCE_COMPILES("
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xlib.h>
int main(void) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  XReparentWindow(info.info.x11.display, info.info.x11.window, (Window)0, 0, 0);
  return 0;
}
" HAVE_SDL_X11_WINDOW)
      SET(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
      SET(CMAKE_REQUIRED_INCLUDES ${OLD_CMAKE_REQUIRED_INCLUDES})
      SET(CMAKE_REQUIRED_LIBRARIES ${OLD_CMAKE_REQUIRED_LIBRARIES})
    ELSE()
      SET(HAVE_SDL_X11_WINDOW FALSE)
    ENDIF()
    IF (HAVE_SDL_X11_WINDOW)
      ADD_DEFINITIONS(-DHAVE_SDL_X11_WINDOW)
      INCLUDE_DIRECTORIES(${X11_INCLUDE_DIR})
      MESSAGE(STATUS "SDL X11 parent support: ON")
    ELSE()
      ADD_DEFINITIONS(-DDIGGER_DISABLE_SDL_X11_WINDOW)
      MESSAGE(STATUS "SDL X11 parent support: OFF")
    ENDIF()
  ENDIF ( NOT SDL2_FOUND )
  ELSE ( NOT DIGGER_VGL )
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_VGL" )
ENDIF ( NOT DIGGER_VGL )

# microsippy sources, as in the GNUmakefile
SET( MSRC microsippy/src )

SET( DIGGER_SOURCE
     main.c
     digger.c
     drawing.c
     sprite.c
     scores.c
     record.c
     edrf.c
     sound.c
     sound_backend.c
     newsnd.c
     ini.c
     input.c
     monster.c
     bags.c
     alpha.c
     vgagrafx.c
     title_gz.c
     icon.c
     sdl_kbd.c
     sdl_vid.c
     sdl_timer.c
     sdl_snd.c
     digger_math.c
     monster_obj.c
     digger_obj.c
     bullet_obj.c
     title_anim.c
     cgagrafx.c
     keyboard.c
     soundgen.c
     spinlock.c
     digger_log.c
     game.c
     netsim_friends.c
     netsim_sip.c
     netsim_sip_registrar.c
     netsim_sip_sdp.c
     netsim_platform.c
     netsim_debug.c
     state_hash.c
     ${MSRC}/external/mackron_md5/md5.c
)

IF ( EMSCRIPTEN )
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       netsim_stubs.c
  )
ELSE ()
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       netsim_rx.c
       netsim.c
  )
ENDIF ()

IF ( NOT MSVC )
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       fbsd_sup.c
  )
ENDIF( NOT MSVC )

IF ( DIGGER_VGL )
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       fbsd_vid.c
       fbsd_kbd.c
       fbsd_timer.c
     )
ENDIF ( DIGGER_VGL )

IF ( EMSCRIPTEN )
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       ems_vid.c
       ems_kbd.c
       ems_store.c
  )
ENDIF ( EMSCRIPTEN )

ADD_EXECUTABLE( digger ${DIGGER_SOURCE})

# Nothing in it is for anybody else to link to (-fvisibility=hidden)
SET_TARGET_PROPERTIES( digger PROPERTIES C_VISIBILITY_PRESET hidden )

# Link-time optimisation, where the toolchain can do it, but for the Debug
# builds
OPTION( DIGGER_LTO "Link-time optimisation" ON )
IF ( DIGGER_LTO )
  INCLUDE( CheckIPOSupported )
  CHECK_IPO_SUPPORTED( RESULT HAVE_IPO OUTPUT IPO_OUTPUT LANGUAGES C )
  IF ( HAVE_IPO )
    SET_TARGET_PROPERTIES( digger PROPERTIES
      INTERPROCEDURAL_OPTIMIZATION TRUE
      INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE )
  ELSE ()
    MESSAGE( STATUS "No link-time optimisation: ${IPO_OUTPUT}" )
  ENDIF ()
ENDIF ()

IF (UNIX AND NOT EMSCRIPTEN)
  SET(DIGGER_SIP_PLATFORM_SOURCE
    microsippy/platforms/posix/usipy_platform_default.c
  )
  SET(DIGGER_SIP_PLATFORM_INCLUDE_DIR
    microsippy/platforms/posix
  )
  SET(DIGGER_SIP_PLATFORM_PORT_INCLUDE_DIR
    microsippy/platforms/posix/usipy_port
  )
ELSEIF (WIN32 AND NOT EMSCRIPTEN)
  SET(DIGGER_SIP_PLATFORM_SOURCE
    microsippy/platforms/win32/usipy_platform_default.c
  )
  SET(DIGGER_SIP_PLATFORM_INCLUDE_DIR
    microsippy/platforms/win32
  )
  SET(DIGGER_SIP_PLATFORM_PORT_INCLUDE_DIR
    microsippy/platforms/win32/usipy_port
  )
ENDIF()

IF ((UNIX OR WIN32) AND NOT EMSCRIPTEN)
  SET(DIGGER_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/digger_version.h")
  ADD_CUSTOM_COMMAND(
    OUTPUT ${DIGGER_VERSION_HEADER}
    COMMAND sh ${DIGGER_SOURCE_DIR}/scripts/gen-build-info.sh ${DIGGER_VERSION_HEADER}
    DEPENDS ${DIGGER_SOURCE_DIR}/scripts/gen-build-info.sh
    WORKING_DIRECTORY ${DIGGER_SOURCE_DIR}
    VERBATIM
  )
  TARGET_SOURCES(digger PRIVATE
    ${DIGGER_VERSION_HEADER}
    netsim_sip_platform.c
    ${MSRC}/usipy_sip_tm.c
    ${MSRC}/usipy_sip_tm_uac.c
    ${MSRC}/usipy_sip_tm_uas.c
    ${MSRC}/usipy_sip_tm_dialog.c
    ${MSRC}/sip_ua/usipy_sip_ua.c
    ${MSRC}/sip_ua/usipy_sip_ua_idle.c
    ${MSRC}/sip_ua/usipy_sip_ua_trying.c
    ${MSRC}/sip_ua/usipy_sip_ua_dialing.c
    ${MSRC}/sip_ua/usipy_sip_ua_connected.c
    ${MSRC}/sip_ua/usipy_sip_ua_disconnected.c
    ${MSRC}/sip_ua/usipy_sip_ua_utils.c
    ${MSRC}/usipy_sip_msg.c
    ${MSRC}/usipy_msg_heap.c
    ${MSRC}/usipy_str.c
    ${MSRC}/usipy_sip_hdr.c
    ${MSRC}/usipy_fast_parser.c
    ${MSRC}/usipy_sip_hdr_db.c
    ${MSRC}/usipy_sip_sline.c
    ${MSRC}/usipy_misc.c
    ${MSRC}/usipy_sip_method_db.c
    ${MSRC}/usipy_sip_hdr_cseq.c
    ${MSRC}/usipy_sip_hdr_via.c
    ${MSRC}/usipy_sip_uri.c
    ${MSRC}/usipy_sip_req.c
    ${MSRC}/usipy_sip_hdr_onetoken.c
    ${MSRC}/usipy_sip_hdr_nameaddr.c
    ${MSRC}/usipy_sip_hdr_auth.c
    ${MSRC}/usipy_sip_hdr_authz.c
    ${MSRC}/usipy_sip_tid.c
    ${MSRC}/usipy_sip_res.c
    ${MSRC}/usipy_sip_response_utils.c
    ${MSRC}/usipy_sip_tm_utils.c
    ${MSRC}/usipy_sip_dialog.c
    ${MSRC}/usipy_platform.c
    microsippy/platforms/posix/usipy_tm_uac.c
    ${DIGGER_SIP_PLATFORM_SOURCE}
  )
  TARGET_INCLUDE_DIRECTORIES(digger PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${MSRC}
    ${DIGGER_SIP_PLATFORM_INCLUDE_DIR}
    ${DIGGER_SIP_PLATFORM_PORT_INCLUDE_DIR}
    microsippy/platforms/posix
    microsippy/platforms/posix/usipy_port
  )
  # microsippy reads words of what it parses in the host's byte order
  INCLUDE(TestBigEndian)
  TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
  IF (IS_BIG_ENDIAN)
    TARGET_COMPILE_DEFINITIONS(digger PRIVATE USIPY_BIGENDIAN=1)
  ELSE()
    TARGET_COMPILE_DEFINITIONS(digger PRIVATE USIPY_BIGENDIAN=0)
  ENDIF()
  TARGET_COMPILE_DEFINITIONS(digger PRIVATE HAVE_CMAKE_SYMBOL_CHECKS=1)
  IF (HAVE_CLOCK_NANOSLEEP)
    TARGET_COMPILE_DEFINITIONS(digger PRIVATE HAVE_CLOCK_NANOSLEEP=1)
  ENDIF()
  IF (HAVE_TIMER_ABSTIME)
    TARGET_COMPILE_DEFINITIONS(digger PRIVATE HAVE_TIMER_ABSTIME=1)
  ENDIF()
  IF (HAVE_PTHREAD_CONDATTR_SETCLOCK)
    TARGET_COMPILE_DEFINITIONS(digger PRIVATE HAVE_PTHREAD_CONDATTR_SETCLOCK=1)
  ENDIF()
ENDIF()

IF ( SDL2_FOUND )
  TARGET_LINK_LIBRARIES( digger PUBLIC SDL2::SDL2 )
ENDIF( SDL2_FOUND )
IF ( WIN32 )
  TARGET_LINK_LIBRARIES( digger PUBLIC ws2_32 )
ENDIF()
IF (HAVE_SDL_X11_WINDOW)
  TARGET_LINK_LIBRARIES( digger PUBLIC ${X11_LIBRARIES} )
ENDIF()
IF ( NOT MSVC )
  TARGET_LINK_LIBRARIES( digger PUBLIC Threads::Threads )
  IF (NEED_LIBATOMIC)
    TARGET_LINK_LIBRARIES( digger PUBLIC -latomic )
  ENDIF (NEED_LIBATOMIC)
ENDIF( NOT MSVC )
TARGET_LINK_LIBRARIES( digger PUBLIC ${ZLIB_LIBRARY} )
TARGET_LINK_LIBRARIES( digger PUBLIC -lm)

INSTALL(TARGETS digger
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
INSTALL(FILES README.md digger.txt
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/${PACKAGE})
