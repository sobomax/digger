PROJECT(DIGGER)
SET( VERSION "20110912" )
SET( PACKAGE "digger" )
SET( CMAKE_MODULE_PATH ${DIGGER_SOURCE_DIR}/cmake/modules )
cmake_minimum_required( VERSION 2.6 )

IF( NOT MSVC )
  SET( WARN_FLAGS "-Wall -Wformat=2" )
  SET( WARN_FLAGS_DEBUG "-Wextra -pedantic" )
  SET( C_FLAGS "-std=c99 -DLINUX -D_REENTRANT -pipe -Wall" )
  SET( CMAKE_C_FLAGS "${C_FLAGS} ${WARN_FLAGS} ${CMAKE_C_FLAGS}" )
  SET( CMAKE_C_FLAGS_DEBUG "${CFLAGS} ${WARN_FLAGS} ${WARN_FLAGS_DEBUG} -g3 -O0 ${CMAKE_C_FLAGS_DEBUG} -DDIGGER_DEBUG" )
ENDIF( NOT MSVC )

SET( CPACK_PACKAGE_VERSION, "${VERSION}" )

SET( CPACK_SOURCE_IGNORE_FILES ".git" "build" ".swp$" ".*~" )

SET( CPACK_PACKAGE_NAME, "${PACKAGE}" )
SET( CPACK_SOURCE_GENERATOR "TGZ;TXZ")
SET( CPACK_SOURCE_PACKAGE_FILE_NAME "${PACKAGE}-${VERSION}" )

INCLUDE(CPack)

IF( NOT DEFINED CMAKE_INSTALL_PREFIX )
  SET( CMAKE_INSTALL_PREFIX /usr/local )
ENDIF( NOT DEFINED CMAKE_INSTALL_PREFIX )

FIND_PACKAGE( ZLIB REQUIRED )
IF ( NOT ZLIB_FOUND )
  MESSAGE( FATAL_ERROR " zlib-devel not found" )
ELSE ( NOT ZLIB_FOUND )
  INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})
ENDIF ( NOT ZLIB_FOUND )

OPTION( DIGGER_VGL
  "Build FreeBSD VGL."
  OFF )
IF ( NOT DIGGER_VGL )
  FIND_PACKAGE( SDL2 REQUIRED )
  IF ( NOT SDL2_FOUND )
    MESSAGE( FATAL_ERROR " SDL2-devel not found" )
  ELSE ( NOT SDL2_FOUND )
    execute_process(COMMAND sdl2-config --cflags RESULT_VARIABLE CMD_RES OUTPUT_VARIABLE SDL2_CFLAGS ERROR_VARIABLE ERR_VAR OUTPUT_STRIP_TRAILING_WHITESPACE)
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SDL2_CFLAGS} -D_SDL" )
    INCLUDE_DIRECTORIES(${SDL2_INCLUDE_DIR})
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

ADD_EXECUTABLE( digger ${DIGGER_SOURCE})

IF ( SDL2_FOUND )
  TARGET_LINK_LIBRARIES( digger ${SDL2_LIBRARY} )
ENDIF( SDL2_FOUND )
TARGET_LINK_LIBRARIES( digger ${ZLIB_LIBRARY} )
TARGET_LINK_LIBRARIES( digger -lm)
