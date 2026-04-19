idf_component_register(SRCS "udp_server.c" "usipy_esp8266_timer1.h"
  INCLUDE_DIRS "." ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src)

add_subdirectory(../../../../src microsippy)
target_include_directories(microsippy PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(${COMPONENT_LIB} PRIVATE microsippy)
set_property(TARGET microsippy PROPERTY C_STANDARD 11)
set_property(TARGET ${COMPONENT_LIB} PROPERTY C_STANDARD 11)
