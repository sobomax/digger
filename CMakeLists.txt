cmake_minimum_required( VERSION 3.5 )
PROJECT(DIGGER)
SET( VERSION "20110912" )
SET( PACKAGE "digger" )
SET( CMAKE_MODULE_PATH ${DIGGER_SOURCE_DIR}/cmake/modules )

IF( NOT MSVC )
  SET( WARN_FLAGS "-Wall -Wformat=2" )
  SET( WARN_FLAGS_DEBUG "-Wextra -pedantic" )
  SET( C_FLAGS "-std=c99 -DLINUX -D_REENTRANT -pipe -Wall" )
  SET( CMAKE_C_FLAGS "${C_FLAGS} ${WARN_FLAGS} ${CMAKE_C_FLAGS}" )
  SET( CMAKE_C_FLAGS_DEBUG "${CFLAGS} ${WARN_FLAGS} ${WARN_FLAGS_DEBUG} -g3 -O0 ${CMAKE_C_FLAGS_DEBUG} -DDIGGER_DEBUG" )
ENDIF( NOT MSVC )

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
INCLUDE(CheckCSourceCompiles)
INCLUDE(CheckCXXSourceCompiles)
INCLUDE(CheckLibraryExists)

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

IF( NOT MSVC )
  check_working_cxx_atomics(HAVE_CXX_ATOMICS_WITHOUT_LIB)
  find_package(Threads REQUIRED)

  IF (NOT HAVE_CXX_ATOMICS_WITHOUT_LIB)
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
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SDL2_CFLAGS} -D_SDL" )
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

SET( DIGGER_SOURCE
     main.c
     digger.c
     drawing.c
     sprite.c
     scores.c
     record.c
     sound.c
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
     cgagrafx.c
     keyboard.c
     soundgen.c
     spinlock.c
     digger_log.c
     game.c
     netsim.c
     netsim_platform.c
     netsim_debug.c
)

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
  )
ENDIF ( EMSCRIPTEN )

ADD_EXECUTABLE( digger ${DIGGER_SOURCE})

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
  IF (NOT HAVE_CXX_ATOMICS_WITHOUT_LIB )
    TARGET_LINK_LIBRARIES( digger PUBLIC -latomic )
  ENDIF ( NOT HAVE_CXX_ATOMICS_WITHOUT_LIB )
ENDIF( NOT MSVC )
TARGET_LINK_LIBRARIES( digger PUBLIC ${ZLIB_LIBRARY} )
TARGET_LINK_LIBRARIES( digger PUBLIC -lm)
