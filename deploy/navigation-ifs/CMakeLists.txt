# navigation.ifs (`make ifs`): the whole navigation code under /open-navicore
# - the provider, its nv_navigation.cfg and boot script, the OsmAnd backend,
# OsmAnd core and its resource bundle (Qt and the C++ runtime are linked
# statically). Data (maps, recents, vector cache, bench markers) stays in
# /datastore/navicore-backend. The image is packaged by ifs-build, the
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
# OsmAnd's resource bundle (built next to the core by the OsmAnd QNX build).
set(OSMAND_RESOURCES_BUNDLE
    ${OSMAND_QNX_ROOT}/source/binaries/qnx/gcc-armv7/Release/libOsmAndCore_ResourcesBundle_shared.so
    CACHE FILEPATH "libOsmAndCore_ResourcesBundle_shared.so for the image")
set(STOCK_NV_NAVIGATION_CFG "" CACHE FILEPATH
    "the unit's factory /pas/scfg/nv_navigation.cfg (QR39.5 revision, not committed)")

if(NOT EXISTS ${IFSBUILD_DIR}/cmake/IfsPackage.cmake OR NOT STOCK_NV_NAVIGATION_CFG
   OR NOT EXISTS ${OSMAND_RESOURCES_BUNDLE})
  add_custom_target(ifs
    COMMAND ${CMAKE_COMMAND} -E echo
      "make ifs needs ifs-build (IFSBUILD_DIR=${IFSBUILD_DIR}), STOCK_NV_NAVIGATION_CFG= and OSMAND_RESOURCES_BUNDLE=${OSMAND_RESOURCES_BUNDLE}"
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

# Mounted at / (vp4r-loader: $VP4R_HOME/slots/<slot>/ifs/50-navicore.ifs, or
# the older boot overlay); every path is under /open-navicore except
# /pas/scfg/nv_navigation.cfg: mounted after the stock images, it shadows the
# factory config, so System Manager starts nv_navigation with Open NaviCore
# without boot.sh's link (boot.sh stays for the older overlay). The
# libraries are stored uncompressed (+raw) to be mapped in place. The provider
# loads the backend from /open-navicore and the backend's RPATH is
# /open-navicore, so the core and bundle are found there.
set(core ${OSMAND_QNX_ROOT}/source/binaries/qnx/gcc-armv7/Release/libOsmAndCore_shared.so)
add_ifs_package(navigation
  MOUNT_POINT /
  OPTIONS -bigendian -autolink -followlink -optional +raw
  FILES
    /open-navicore/libNaviCore.so=${payload}/libNaviCore.so
    /open-navicore/libOpenNaviBackendOsmAnd.so=$<TARGET_FILE:osmand_backend>
    /open-navicore/libOsmAndCore_shared.so=${core}
    /open-navicore/libOsmAndCore_ResourcesBundle_shared.so=${OSMAND_RESOURCES_BUNDLE}
    /open-navicore/nv_navigation.cfg=${payload}/nv_navigation.cfg
    /pas/scfg/nv_navigation.cfg=${payload}/nv_navigation.cfg
    /open-navicore/.open_navicore_navigation_v1=${payload}/marker.txt
    /open-navicore/boot.sh=${CMAKE_CURRENT_SOURCE_DIR}/boot.sh
    /open-navicore/.ifs_padding.txt=${payload}/padding.txt
  TARGET ifs)
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/ifs/navigation.ifs APPEND
  DEPENDS ${payload_files} ${CMAKE_CURRENT_SOURCE_DIR}/boot.sh ${core}
          ${OSMAND_RESOURCES_BUNDLE})
# Install: deploy/navigation-ifs/deploy_navigation_ifs.sh <image> <ifs-deploy>
# (staged; ifs-build's generic deploy-ifs target would upload to the wrong
# directory).
