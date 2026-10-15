# navigation.ifs (`make ifs`): the provider, its nv_navigation.cfg and boot
# script under /open-navicore. The image is packaged by ifs-build, the
# workspace's IFS packaging project (a sibling checkout ../ifs-build, like
# dev-tools; IFSBUILD_DIR to override).
if(NOT IFSBUILD_DIR)
  if(DEFINED ENV{IFSBUILD_DIR})
    set(IFSBUILD_DIR $ENV{IFSBUILD_DIR})
  else()
    get_filename_component(IFSBUILD_DIR ${ONC_ROOT}/../ifs-build ABSOLUTE)
  endif()
endif()
set(IFSBUILD_DIR ${IFSBUILD_DIR} CACHE PATH "ifs-build checkout")
set(STOCK_NV_NAVIGATION_CFG "" CACHE FILEPATH
    "the unit's factory /pas/scfg/nv_navigation.cfg (QR39.5 revision, not committed)")

if(NOT EXISTS ${IFSBUILD_DIR}/cmake/IfsPackage.cmake OR NOT STOCK_NV_NAVIGATION_CFG)
  add_custom_target(ifs
    COMMAND ${CMAKE_COMMAND} -E echo
      "make ifs needs ifs-build (IFSBUILD_DIR=${IFSBUILD_DIR}) and STOCK_NV_NAVIGATION_CFG="
    COMMAND ${CMAKE_COMMAND} -E false)
  return()
endif()
list(APPEND CMAKE_MODULE_PATH ${IFSBUILD_DIR}/cmake)
include(IfsPackage)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Payload: the provider, the config pointing at it and the boot marker
# (build_navigation_ifs.py checks the inputs).
set(payload ${CMAKE_CURRENT_BINARY_DIR}/payload)
set(payload_files ${payload}/libNaviCore.so ${payload}/nv_navigation.cfg
    ${payload}/marker.txt ${payload}/padding.txt)
add_custom_command(
  OUTPUT ${payload_files}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/build_navigation_ifs.py
          $<TARGET_FILE:provider> ${STOCK_NV_NAVIGATION_CFG} ${payload}
  DEPENDS provider ${CMAKE_CURRENT_SOURCE_DIR}/build_navigation_ifs.py
          ${STOCK_NV_NAVIGATION_CFG}
  COMMENT "navigation.ifs payload"
  VERBATIM)

# Mounted at / by the boot overlay; every path is under /open-navicore. The
# provider must be stored uncompressed (+raw) to be mapped in place.
add_ifs_package(navigation
  MOUNT_POINT /
  OPTIONS -bigendian -autolink -followlink -optional +raw
  FILES
    /open-navicore/libNaviCore.so=${payload}/libNaviCore.so
    /open-navicore/nv_navigation.cfg=${payload}/nv_navigation.cfg
    /open-navicore/.open_navicore_navigation_v1=${payload}/marker.txt
    /open-navicore/boot.sh=${CMAKE_CURRENT_SOURCE_DIR}/boot.sh
    /open-navicore/.ifs_padding.txt=${payload}/padding.txt
  TARGET ifs)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/ifs/navigation.ifs APPEND
  DEPENDS ${payload_files} ${CMAKE_CURRENT_SOURCE_DIR}/boot.sh)
# Install: deploy/navigation-ifs/deploy_navigation_ifs.sh <image> <ifs-deploy>
# (staged; ifs-build's generic deploy-ifs target would upload to the wrong
# directory).
