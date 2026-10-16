# Open NaviCore. Use the top-level Makefile:
#
#   make            the product for the radio (QNX toolchain, build/qnx):
#                   provider libNaviCore.so, the OsmAnd backend, the reference
#                   backends and the on-target smoke programs
#   make ifs        navigation.ifs (provider image, via ifs-build)
#   make test       host tests and contract checks (native compiler, build/host)
#
# The same CMakeLists builds the product when configured with
# cmake/qnx-armv7.cmake, and the host tests otherwise.
cmake_minimum_required(VERSION 3.21)

# Host builds use ccache when installed (the QNX toolchain file does the same).
if(NOT CMAKE_TOOLCHAIN_FILE)
  find_program(ONC_CCACHE ccache)
  if(ONC_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${ONC_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${ONC_CCACHE})
  endif()
endif()

project(open_navicore C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(ONC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(ONC_WARNINGS -Wall -Wextra -Werror)

if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  set(ONC_QNX ON)
  # The QNX sysroot's integer typedefs carry attributes that GCC 8 reports on
  # every template use (-Wignored-attributes); that noise is not ours.
  set(ONC_TARGET_FLAGS -O2 -g -fPIC -ffunction-sections -fdata-sections
      $<$<COMPILE_LANGUAGE:CXX>:-Wno-ignored-attributes>)
  # The C parts (provider, reference backends, smokes) are Thumb-2, like the
  # QNX qcc builds they replace; the OsmAnd backend stays ARM.
  set(ONC_THUMB -mthumb)
  add_subdirectory(src)
  add_subdirectory(backends/reference)
  add_subdirectory(backends/osmand)
  add_subdirectory(tests)
  add_subdirectory(deploy/navigation-ifs)
  add_subdirectory(deploy/bench)
else()
  set(ONC_QNX OFF)
  enable_testing()
  add_subdirectory(backends/osmand)
  add_subdirectory(tests)
endif()
