# navigation.ifs: WHAT goes into the image. HOW (toolchain, mkifs) comes from
# ifs-build, the workspace's IFS packaging project (a sibling checkout of this
# repository by default, like dev-tools; override with -DIFSBUILD_DIR= or
# $IFSBUILD_DIR, e.g. from a linked worktree).
#
#   cmake -S deploy/navigation-ifs -B build/navigation-ifs \
#         -DSTOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg>
#   cmake --build build/navigation-ifs --target ifs        # ifs/navigation.ifs
#   cmake --build build/navigation-ifs --target deploy-navigation
#
# The provider comes from `make` (dist/libNaviCore.so); a newer provider
# rebuilds the payload and the image.
cmake_minimum_required(VERSION 3.19)

get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(DEFINED ENV{IFSBUILD_DIR})
  set(_ifs_default "$ENV{IFSBUILD_DIR}")
else()
  get_filename_component(_ifs_default "${_repo}/../ifs-build" ABSOLUTE)
endif()
set(IFSBUILD_DIR "${_ifs_default}" CACHE PATH "ifs-build checkout")
if(NOT EXISTS "${IFSBUILD_DIR}/cmake/IfsPackage.cmake")
  message(FATAL_ERROR "ifs-build not found at ${IFSBUILD_DIR}; set -DIFSBUILD_DIR=")
endif()
set(CMAKE_TOOLCHAIN_FILE ${IFSBUILD_DIR}/cmake/toolchains/qnx660-armle-v7.cmake)
project(open_navicore_navigation NONE)
list(APPEND CMAKE_MODULE_PATH ${IFSBUILD_DIR}/cmake)
include(IfsPackage)

set(OPEN_NAVICORE_PROVIDER "${_repo}/dist/libNaviCore.so" CACHE FILEPATH
    "Open NaviCore provider (make)")
set(STOCK_NV_NAVIGATION_CFG "" CACHE FILEPATH
    "the unit's factory /pas/scfg/nv_navigation.cfg (QR39.5 revision, not committed)")
if(NOT STOCK_NV_NAVIGATION_CFG)
  message(FATAL_ERROR "set -DSTOCK_NV_NAVIGATION_CFG=<factory nv_navigation.cfg>")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Payload: the provider, the config pointing at it, the boot marker and
# padding (build_navigation_ifs.py checks the inputs).
set(payload "${CMAKE_BINARY_DIR}/payload")
set(payload_files ${payload}/libNaviCore.so ${payload}/nv_navigation.cfg
    ${payload}/marker.txt ${payload}/padding.txt)
add_custom_command(
  OUTPUT ${payload_files}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/build_navigation_ifs.py
          ${OPEN_NAVICORE_PROVIDER} ${STOCK_NV_NAVIGATION_CFG} ${payload}
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/build_navigation_ifs.py
          ${OPEN_NAVICORE_PROVIDER} ${STOCK_NV_NAVIGATION_CFG}
  COMMENT "navigation.ifs payload"
  VERBATIM)

# Mounted at / by the boot hook; every path is under /open-navicore. The
# provider must be stored uncompressed (+raw) to be mapped in place; the
# whole image is raw, which costs only the 16 KiB padding.
add_ifs_package(navigation
  MOUNT_POINT /
  OPTIONS -bigendian -autolink -followlink -optional +raw
  FILES
    /open-navicore/libNaviCore.so=${payload}/libNaviCore.so
    /open-navicore/nv_navigation.cfg=${payload}/nv_navigation.cfg
    /open-navicore/.open_navicore_navigation_v1=${payload}/marker.txt
    /open-navicore/boot.sh=${CMAKE_CURRENT_LIST_DIR}/boot.sh
    /open-navicore/.ifs_padding.txt=${payload}/padding.txt
  TARGET ifs)
# Plain files are not dependencies of the mkifs step; add the payload.
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/ifs/navigation.ifs APPEND
  DEPENDS ${payload_files} ${CMAKE_CURRENT_LIST_DIR}/boot.sh)

# The image lives in /datastore/navicore (the boot hook's path) and is
# mounted at boot, so it is replaced staged: upload as .new with ifs-deploy,
# keep a copy of the old one, then swap by rm + cp -p (never in place).
add_custom_target(deploy-navigation
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/deploy_navigation_ifs.sh
          ${CMAKE_BINARY_DIR}/ifs/navigation.ifs ${IFSBUILD_DIR}/bin/ifs-deploy
  COMMENT "deploy navigation.ifs (staged; reboot once to activate)"
  USES_TERMINAL VERBATIM)
add_dependencies(deploy-navigation ifs)
add_custom_target(deploy-navigation-dry-run
  COMMAND ${CMAKE_CURRENT_LIST_DIR}/deploy_navigation_ifs.sh
          ${CMAKE_BINARY_DIR}/ifs/navigation.ifs ${IFSBUILD_DIR}/bin/ifs-deploy --dry-run
  USES_TERMINAL VERBATIM)
add_dependencies(deploy-navigation-dry-run ifs)
# Note: add_ifs_package also defines `deploy-ifs`, which would upload to
# /datastore/ifs-overrides where the boot hook does not look. Use
# deploy-navigation.
