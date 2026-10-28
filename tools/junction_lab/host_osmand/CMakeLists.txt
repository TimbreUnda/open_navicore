# Host (x86_64 Linux) build of OsmAnd core-legacy's routing subset: the real router and
# route preparation (turn types, attached roads) on local OBF maps. Debugging aid only.
#
#   cmake -S tools/junction_lab/host_osmand -B build/junction_lab/host_osmand -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/junction_lab/host_osmand -j
#
# Outputs (in the build dir): libosmand_routing_host.a, libprotobuf_host.a, libexpat_host.a,
# route_bench_host (deploy/bench/route_bench.cpp + backends/osmand/ch_planner.cpp, unchanged).
cmake_minimum_required(VERSION 3.16)
project(osmand_routing_host C CXX)

get_filename_component(ONC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
# The OsmAnd source tree (read-only), as cmake/qnx-armv7.cmake finds the OsmAnd QNX
# build: $OSMAND_QNX_ROOT, else the directory that holds this repository (set it
# from a linked worktree).
if(DEFINED ENV{OSMAND_QNX_ROOT})
  set(_osmand_qnx_root $ENV{OSMAND_QNX_ROOT})
else()
  get_filename_component(_osmand_qnx_root ${ONC_ROOT}/../osmand-qnx-build ABSOLUTE)
endif()
set(OSMAND_SOURCE_ROOT ${_osmand_qnx_root}/source CACHE PATH "OsmAnd source tree (read-only)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(_legacy ${OSMAND_SOURCE_ROOT}/core-legacy/native)
set(_ext ${OSMAND_SOURCE_ROOT}/core/externals)
set(_pb ${_ext}/protobuf/upstream.patched/src/google/protobuf)

# ---- protobuf 2.5 (same file list as core/externals/protobuf) ----------------------------
add_library(protobuf_host STATIC
  ${_pb}/descriptor.cc ${_pb}/descriptor_database.cc ${_pb}/descriptor.pb.cc
  ${_pb}/dynamic_message.cc ${_pb}/extension_set.cc ${_pb}/extension_set_heavy.cc
  ${_pb}/generated_message_reflection.cc ${_pb}/generated_message_util.cc ${_pb}/message.cc
  ${_pb}/message_lite.cc ${_pb}/reflection_ops.cc ${_pb}/repeated_field.cc ${_pb}/service.cc
  ${_pb}/text_format.cc ${_pb}/unknown_field_set.cc ${_pb}/wire_format.cc
  ${_pb}/wire_format_lite.cc ${_pb}/io/coded_stream.cc ${_pb}/stubs/common.cc
  ${_pb}/stubs/once.cc ${_pb}/stubs/atomicops_internals_x86_gcc.cc ${_pb}/stubs/stringprintf.cc
  ${_pb}/stubs/structurally_valid.cc ${_pb}/stubs/strutil.cc ${_pb}/stubs/substitute.cc
  ${_pb}/io/zero_copy_stream_impl.cc ${_pb}/io/printer.cc ${_pb}/io/tokenizer.cc
  ${_pb}/io/zero_copy_stream_impl_lite.cc ${_pb}/io/zero_copy_stream.cc)
target_include_directories(protobuf_host PUBLIC ${_ext}/protobuf/upstream.patched/src
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/config)
target_compile_definitions(protobuf_host PRIVATE __OSMAND_DUMMY__)
target_compile_options(protobuf_host PRIVATE -w -O2)
set_target_properties(protobuf_host PROPERTIES CXX_STANDARD 11)

# ---- expat -----------------------------------------------------------------------------
add_library(expat_host STATIC
  ${_ext}/expat/upstream.patched/lib/xmlparse.c ${_ext}/expat/upstream.patched/lib/xmlrole.c
  ${_ext}/expat/upstream.patched/lib/xmltok.c)
target_include_directories(expat_host PUBLIC ${_ext}/expat/upstream.patched/lib
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/config)
target_compile_definitions(expat_host PRIVATE HAVE_EXPAT_CONFIG_H PUBLIC XML_STATIC)
target_compile_options(expat_host PRIVATE -w -O2)

# ---- core-legacy routing ---------------------------------------------------------------
set(_s ${_legacy}/src)
add_library(osmand_routing_host STATIC
  ${_s}/proto/OBF.pb.cc ${_s}/proto/osmand_index.pb.cc
  ${_s}/openingHoursParser.cpp ${_s}/multipolygons.cpp ${_s}/common.cpp ${_s}/ElapsedTimer.cpp
  ${_s}/routeTypeRule.cpp ${_s}/binaryRead.cpp ${_s}/binaryRoutePlanner.cpp
  ${_s}/generalRouter.cpp ${_s}/turnType.cpp ${_s}/routeResultPreparation.cpp
  ${_s}/routeSegmentResult.cpp ${_s}/routingConfiguration.cpp
  ${_s}/precalculatedRouteDirection.cpp ${_s}/routePlannerFrontEnd.cpp ${_s}/routingContext.cpp
  ${_s}/routeDataBundle.cpp ${_s}/routeDataResources.cpp ${_s}/routeCalculationProgress.cpp
  ${_s}/renderRules.cpp ${_s}/hhRouteDataStructure.cpp ${_s}/hhRoutePlanner.cpp
  ${_s}/NetworkDBPointRouteInfo.cpp ${_s}/roundaboutTurn.cpp
  ${_s}/gpxRouteApproximation.cpp ${_s}/gpxMultiSegmentsApproximation.cpp
  ${_s}/gpxSimplePointsMatchApproximation.cpp
  ${_s}/transportRoutePlanner.cpp ${_s}/transportRoutingConfiguration.cpp
  ${_s}/transportRouteResult.cpp ${_s}/transportRouteResultSegment.cpp
  ${_s}/transportRouteSegment.cpp ${_s}/transportRoutingContext.cpp
  ${_s}/transportRoutingObjects.cpp ${_s}/transportRouteStopsReader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/logging_stub.cpp ${CMAKE_CURRENT_SOURCE_DIR}/skia_stub.cpp)

# What a consumer needs (shim first: replaces the Qt-based <OsmAndCore/Logging.h>).
set(OSMAND_HOST_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/shim
  ${_legacy}/include ${_legacy}/src
  ${_ext}/protobuf/upstream.patched/src
  ${_ext}/boost/upstream.patched
  ${_ext}/skia/upstream.patched ${_ext}/skia/upstream.patched/include/core
  ${_ext}/skia/upstream.patched/include/effects ${_ext}/skia/upstream.patched/src/core
  ${_ext}/expat/upstream.patched/lib
  ${_ext}/harfbuzz/upstream.patched/src ${_ext}/freetype/upstream.patched/include)
set(OSMAND_HOST_DEFINES _HAS_C9X=1 _NO_CPP_INLINES=1 _HAS_GENERIC_TEMPLATES=0
  OSMAND_COMPILER_FAMILY_gcc OSMAND_TARGET_OS_linux OSMAND_TARGET_CPU_ARCH_x64
  OSMAND_TARGET_CPU_ARCH_FAMILY_x86 SK_SUPPORT_LEGACY_DRAWLOOPER SK_RELEASE)
target_include_directories(osmand_routing_host PUBLIC ${OSMAND_HOST_INCLUDES})
target_compile_definitions(osmand_routing_host PUBLIC ${OSMAND_HOST_DEFINES})
target_compile_options(osmand_routing_host PRIVATE -w -O2 -g)
find_package(Threads REQUIRED)
target_link_libraries(osmand_routing_host PUBLIC protobuf_host expat_host Threads::Threads)

# ---- route_bench on the host -------------------------------------------------------------
add_executable(route_bench_host ${ONC_ROOT}/deploy/bench/route_bench.cpp
  ${ONC_ROOT}/backends/osmand/ch_planner.cpp ${CMAKE_CURRENT_SOURCE_DIR}/route_bench_host_main.cpp)
target_compile_options(route_bench_host PRIVATE -w -O2 -g)
target_link_libraries(route_bench_host PRIVATE osmand_routing_host ${CMAKE_DL_LIBS} m)

# ---- junction lab (tools/junction_lab) ---------------------------------------------------
# The backend's manoeuvre and cluster code on top of the host router.
set(_backend ${ONC_ROOT}/backends/osmand)
# Organic Maps' car turn generator (third_party/organicmaps-routing, Apache-2.0) on the
# OsmAnd route, through the backend's own omaps_turns.cpp, as on the radio.
include(${ONC_ROOT}/third_party/organicmaps-routing/sources.cmake)
add_library(organicmaps_turns STATIC ${_backend}/omaps_turns.cpp ${ONC_ORGANICMAPS_SOURCES})
target_include_directories(organicmaps_turns PRIVATE ${ONC_ORGANICMAPS_INCLUDES} ${_backend})
target_compile_options(organicmaps_turns PRIVATE -O2 -g -w)
target_link_libraries(organicmaps_turns PRIVATE osmand_routing_host)

add_executable(junction_lab_tool ${ONC_ROOT}/tools/junction_lab/junction_lab_tool.cpp
  ${_backend}/route_maneuvers.cpp ${_backend}/junction_shape.cpp ${_backend}/cluster_payload.cpp)
target_include_directories(junction_lab_tool PRIVATE ${_backend} ${ONC_ROOT}/include)
target_compile_options(junction_lab_tool PRIVATE -O2 -g)
target_link_libraries(junction_lab_tool PRIVATE organicmaps_turns osmand_routing_host m)
set_target_properties(junction_lab_tool PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ONC_ROOT}/build/junction_lab)

# The classifier alone, for edited angles (no router).
add_executable(junction_probe ${ONC_ROOT}/tools/junction_lab/junction_probe.cpp
  ${_backend}/junction_shape.cpp ${_backend}/cluster_payload.cpp)
target_include_directories(junction_probe PRIVATE ${_backend} ${ONC_ROOT}/include)
set_target_properties(junction_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ONC_ROOT}/build/junction_lab)
