cmake_minimum_required(VERSION 3.5)
if (POLICY CMP0069)
  cmake_policy(SET CMP0069 NEW)
endif()
project (usipy_test)
find_package (Threads)
include (CheckIPOSupported)

include (TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)

add_executable(usipy_test "usipy_test.c" "usipy_stdout_watch.c")
add_executable(usipy_test_uac "usipy_test_uac.c" "usipy_tm_uac.c")
add_executable(usipy_test_ua "usipy_test_ua.c")
add_executable(usipy_register_cli "usipy_register_cli.c" "usipy_tm_uac.c" "usipy_tm_uac_cli.c")
add_executable(ua_cli "ua_cli.c" "usipy_tm_uac.c" "usipy_tm_uac_cli.c")
add_subdirectory(../../src microsippy)
target_include_directories(usipy_test PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_include_directories(usipy_test_uac PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_include_directories(usipy_test_ua PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_include_directories(usipy_register_cli PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_include_directories(ua_cli PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
target_include_directories(microsippy PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_sources(microsippy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/usipy_log.c")
target_link_libraries(usipy_test PRIVATE
  microsippy
  ${CMAKE_THREAD_LIBS_INIT}
)
target_link_libraries(usipy_test_uac PRIVATE
  microsippy
)
target_link_libraries(usipy_test_ua PRIVATE
  microsippy
)
target_link_libraries(usipy_register_cli PRIVATE
  microsippy
)
target_link_libraries(ua_cli PRIVATE
  microsippy
)

set(POSIX_TARGETS
  microsippy
  usipy_test
  usipy_test_uac
  usipy_test_ua
  usipy_register_cli
  ua_cli
)

foreach(target ${POSIX_TARGETS})
  set_property(TARGET ${target} PROPERTY C_STANDARD 11)
  target_compile_options(${target} PRIVATE
    -march=native
    -O2
    -g3
    -fvisibility=hidden
  )
  set_target_properties(${target} PROPERTIES
    C_VISIBILITY_PRESET hidden
  )
endforeach()

IF (IS_BIG_ENDIAN)
    target_compile_definitions(microsippy PRIVATE -DUSIPY_BIGENDIAN=1)
    target_compile_definitions(usipy_test PRIVATE -DUSIPY_BIGENDIAN=1)
    target_compile_definitions(usipy_test_uac PRIVATE -DUSIPY_BIGENDIAN=1)
    target_compile_definitions(usipy_test_ua PRIVATE -DUSIPY_BIGENDIAN=1)
    target_compile_definitions(usipy_register_cli PRIVATE -DUSIPY_BIGENDIAN=1)
    target_compile_definitions(ua_cli PRIVATE -DUSIPY_BIGENDIAN=1)
ELSE (IS_BIG_ENDIAN)
    target_compile_definitions(microsippy PRIVATE -DUSIPY_BIGENDIAN=0)
    target_compile_definitions(usipy_test PRIVATE -DUSIPY_BIGENDIAN=0)
    target_compile_definitions(usipy_test_uac PRIVATE -DUSIPY_BIGENDIAN=0)
    target_compile_definitions(usipy_test_ua PRIVATE -DUSIPY_BIGENDIAN=0)
    target_compile_definitions(usipy_register_cli PRIVATE -DUSIPY_BIGENDIAN=0)
    target_compile_definitions(ua_cli PRIVATE -DUSIPY_BIGENDIAN=0)
ENDIF (IS_BIG_ENDIAN)

enable_testing()

add_test(basic ${CMAKE_SOURCE_DIR}/../../scripts/do-test.sh ${CMAKE_SOURCE_DIR})

check_ipo_supported(RESULT USIPY_POSIX_IPO_SUPPORTED OUTPUT USIPY_POSIX_IPO_OUTPUT LANGUAGES C)
if (USIPY_POSIX_IPO_SUPPORTED)
  foreach(target ${POSIX_TARGETS})
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endforeach()
else()
  message(STATUS "IPO/LTO disabled for POSIX targets: ${USIPY_POSIX_IPO_OUTPUT}")
endif()
