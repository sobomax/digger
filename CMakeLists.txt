cmake_minimum_required(VERSION 2.6)

PROJECT(digger)
SET( PACKAGE "digger" )

SET(VERSION "20110914")

IF(NOT MSVC)
  SET( WARN_FLAGS "-Wall -Wextra -pedantic -Wformat=2" )
  SET( CMAKE_C_FLAGS "${WARN_FLAGS} -g -std=c99 ${CMAKE_C_FLAGS}" )
  SET( CMAKE_C_FLAGS_DEBUG "${WARN_FLAGS_DEBUG} -g -O0 ${CMAKE_CXX_FLAGS_DEBUG}" )
ENDIF(NOT MSVC)

SET(CPACK_PACKAGE_VERSION, "${VERSION}")

SET(CPACK_SOURCE_IGNORE_FILES ".git" "build" ".swp$" ".*~" )

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


OPTION(DIGGER_VGL
  "Build FreeBSD VGL."
  OFF)
IF ( NOT DIGGER_VGL )
  FIND_PACKAGE( SDL REQUIRED )
  IF ( NOT SDL_FOUND )
    MESSAGE( FATAL_ERROR " SDL-devel not found" )
  ELSE ( NOT SDL_FOUND )
    SET (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_SDL" )
    INCLUDE_DIRECTORIES(${SDL_INCLUDE_DIR})
  ENDIF ( NOT SDL_FOUND )
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
)

IF(NOT MSVC)
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       fbsd_sup.c
  )
ENDIF(NOT MSVC)

IF (DIGGER_VGL)
  SET( DIGGER_SOURCE
       ${DIGGER_SOURCE}
       fbsd_vid.c
       fbsd_kbd.c
       fbsd_timer.c
     )
ENDIF (DIGGER_VGL)


ADD_EXECUTABLE(digger ${DIGGER_SOURCE})
TARGET_LINK_LIBRARIES(digger ${SDL_LIBRARY} )
TARGET_LINK_LIBRARIES(digger ${ZLIB_LIBRARY} )
