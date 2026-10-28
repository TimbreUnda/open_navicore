# Organic Maps' car turn generator (Apache-2.0, see README.md): the sources and
# include directories, for the backend (backends/osmand) and the host junction lab
# (tools/junction_lab/host_osmand). The shim directory stands in for OM's base,
# geometry, indexer and platform libraries; keep it private to the targets that
# compile these files.
set(ONC_ORGANICMAPS_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(ONC_ORGANICMAPS_SOURCES
  ${ONC_ORGANICMAPS_ROOT}/routing/car_directions.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/directions_engine.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/routing_settings.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/turns.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/turns_generator.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/turns_generator_utils.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/lanes/lane_info.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/lanes/lane_way.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/lanes/lanes_parser.cpp
  ${ONC_ORGANICMAPS_ROOT}/routing/lanes/lanes_recommendation.cpp)
set(ONC_ORGANICMAPS_INCLUDES ${ONC_ORGANICMAPS_ROOT} ${ONC_ORGANICMAPS_ROOT}/shim)
