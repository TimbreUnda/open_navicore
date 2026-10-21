if(ONC_QNX)
  # ---- On-target smoke programs (copied to the radio by hand) -------------
  add_executable(navicore_smoke qnx_dlopen_smoke.c)
  add_executable(plugin_smoke qnx_plugin_dlopen_smoke.c)
  add_executable(osmand_plugin_smoke qnx_osmand_plugin_smoke.c)
  foreach(t navicore_smoke plugin_smoke osmand_plugin_smoke)
    target_include_directories(${t} PRIVATE ${ONC_ROOT}/include ${ONC_ROOT}/src/include)
    target_compile_options(${t} PRIVATE ${ONC_WARNINGS} ${ONC_THUMB})
    target_link_libraries(${t} PRIVATE c)
  endforeach()
  foreach(t obf route search address_dump search_a)
    add_executable(osmand_${t}_smoke qnx_osmand_${t}_smoke.cpp)
    target_include_directories(osmand_${t}_smoke PRIVATE ${ONC_ROOT}/include ${ONC_OSMAND_INCLUDES}
                               ${ONC_ROOT}/backends/osmand)
    target_compile_definitions(osmand_${t}_smoke PRIVATE ${ONC_OSMAND_DEFINES})
    target_compile_options(osmand_${t}_smoke PRIVATE ${ONC_WARNINGS} ${ONC_OSMAND_FLAGS})
    target_link_options(osmand_${t}_smoke PRIVATE -static-libstdc++ -static-libgcc
      -Wl,--allow-shlib-undefined -Wl,-rpath,/open-navicore)
    target_link_libraries(osmand_${t}_smoke PRIVATE osmand_core socket slog2 z pps EGL GLESv2 m c)
    set_target_properties(osmand_${t}_smoke PROPERTIES SKIP_BUILD_RPATH ON)
  endforeach()
  # Search option B: the prebuilt index (search_proto/), no OsmAnd.
  # The navigation's index search (backends/osmand/search) on a query set.
  add_executable(search_index_bench search_index_bench.cpp
                 ${ONC_ROOT}/backends/osmand/search/search_index.cpp)
  target_include_directories(search_index_bench PRIVATE ${ONC_ROOT}/backends/osmand)
  target_compile_options(search_index_bench PRIVATE ${ONC_WARNINGS} ${ONC_TARGET_FLAGS})
  target_link_options(search_index_bench PRIVATE -static-libstdc++ -static-libgcc)
  target_link_libraries(search_index_bench PRIVATE m c)
  add_executable(search_b search_proto/search_b.cpp)
  target_include_directories(search_b PRIVATE ${ONC_ROOT}/backends/osmand)
  target_compile_options(search_b PRIVATE ${ONC_WARNINGS} ${ONC_TARGET_FLAGS})
  target_link_options(search_b PRIVATE -static-libstdc++ -static-libgcc)
  target_link_libraries(search_b PRIVATE m c)
  # The plugin ABI headers compile for the target (static layout asserts).
  add_library(backend_abi OBJECT backend_abi.c)
  target_include_directories(backend_abi PRIVATE ${ONC_ROOT}/include)
  target_compile_options(backend_abi PRIVATE ${ONC_WARNINGS})
  return()
endif()

# ---- Host tests (make test) ----------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(src ${ONC_ROOT}/src)
set(HOST_RENDERER_SOURCES ${src}/renderer/open_navi_renderer.c ${src}/renderer/open_navi_renderer_backend_table.c ${src}/renderer/open_navi_renderer_pointer.c ${src}/renderer/open_navi_renderer_dispatch.c ${src}/renderer/open_navi_renderer_lifecycle.c ${src}/renderer/open_navi_screen_input.c
    ${src}/backend/open_navi_default_backend.c ${src}/platform/host/open_navi_renderer_stub.c)
set(HOST_PROVIDER_SOURCES ${src}/compat/qr39/navicore_compat.c ${src}/runtime/open_navi_api_telemetry.c
    ${src}/runtime/open_navi_callback_backend.c ${src}/runtime/open_navi_cluster_backend.c
    ${src}/runtime/open_navi_cluster_agent.c ${src}/runtime/open_navi_cluster_relay.c ${ONC_CAGENT_SOURCES}
    ${src}/backend/open_navi_backend_loader.c ${src}/compat/qr39/host_callbacks.c
    ${src}/runtime/open_navi_gps_backend.c ${src}/runtime/open_navi_pointer_backend.c
    ${src}/runtime/open_navi_scalar_backend.c ${HOST_RENDERER_SOURCES})
set(PROVIDER_INCLUDES ${ONC_ROOT}/include ${src}/include ${src}/compat/qr39 ${src}/runtime ${src}/backend
    ${src}/renderer ${ONC_CAGENT_INCLUDES})

# onc_test(<name> SOURCES ... [DEFINES ...] [INCLUDES ...] [LIBS ...])
function(onc_test name)
  cmake_parse_arguments(T "" "" "SOURCES;DEFINES;INCLUDES;LIBS" ${ARGN})
  add_executable(${name} ${T_SOURCES})
  target_include_directories(${name} PRIVATE ${PROVIDER_INCLUDES} ${T_INCLUDES})
  target_compile_definitions(${name} PRIVATE ${T_DEFINES} ${ONC_CAGENT_DEFINES})
  target_compile_options(${name} PRIVATE ${ONC_WARNINGS} -O2 -g)
  target_link_libraries(${name} PRIVATE Threads::Threads ${CMAKE_DL_LIBS} ${T_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

set(stub OPEN_NAVI_TESTING OPEN_NAVI_RENDERER_STUB)
onc_test(host_smoke SOURCES ${HOST_PROVIDER_SOURCES} smoke.c
         DEFINES OPEN_NAVI_DISABLE_LOG ${stub})
onc_test(lifecycle_smoke SOURCES ${HOST_PROVIDER_SOURCES} lifecycle_smoke.c
         DEFINES OPEN_NAVI_DISABLE_LOG ${stub})
onc_test(pas_callbacks_smoke SOURCES ${src}/runtime/open_navi_callback_backend.c
         ${src}/compat/qr39/host_callbacks.c pas_callbacks_smoke.c DEFINES OPEN_NAVI_TESTING)
onc_test(cluster_backend_smoke SOURCES ${src}/runtime/open_navi_cluster_backend.c
         ${src}/runtime/open_navi_cluster_agent.c ${src}/runtime/open_navi_cluster_relay.c ${ONC_CAGENT_SOURCES} cluster_backend_smoke.c)
onc_test(cluster_agent_client_smoke SOURCES ${src}/runtime/open_navi_cluster_backend.c
         ${src}/runtime/open_navi_cluster_agent.c ${src}/runtime/open_navi_cluster_relay.c ${ONC_CAGENT_SOURCES} cluster_agent_client_smoke.c
         DEFINES OPEN_NAVI_TESTING)
onc_test(screen_input_smoke SOURCES ${src}/renderer/open_navi_screen_input.c screen_input_smoke.c)
onc_test(default_backend_gl_smoke SOURCES ${src}/backend/open_navi_default_backend.c
         default_backend_gl_smoke.c INCLUDES fixtures/fake_gles)
onc_test(reference_backend_gl_smoke SOURCES ${ONC_ROOT}/backends/reference/reference_backend.c
         reference_backend_gl_smoke.c INCLUDES fixtures/fake_gles)
onc_test(backend_seam_smoke SOURCES ${HOST_RENDERER_SOURCES} backend_seam_smoke.c DEFINES ${stub})
onc_test(backend_runtime_stress SOURCES ${HOST_RENDERER_SOURCES} backend_runtime_stress.c
         DEFINES ${stub})
onc_test(trace_smoke SOURCES ${HOST_PROVIDER_SOURCES} trace_smoke.c
         DEFINES OPEN_NAVI_TRACE_PATH="${CMAKE_CURRENT_BINARY_DIR}/trace-smoke.log"
         OPEN_NAVI_DISABLE_SYSLOG OPEN_NAVI_RENDERER_STUB)
add_test(NAME plugin_tests COMMAND sh tests/run_plugin_tests.sh ${CMAKE_CURRENT_BINARY_DIR}
         WORKING_DIRECTORY ${ONC_ROOT})
set_tests_properties(plugin_tests PROPERTIES ENVIRONMENT "CC=${CMAKE_C_COMPILER}")

# OsmAnd backend logic and UI (no OsmAnd needed).
onc_test(osmand_session_test SOURCES ${ONC_ROOT}/backends/osmand/config.cpp
         ${ONC_ROOT}/backends/osmand/session.cpp ${ONC_ROOT}/backends/osmand/osmand_backend.cpp
         osmand_session_test.cpp INCLUDES ${ONC_ROOT}/backends/osmand fixtures/fake_egl)
onc_test(navigation_ui_test SOURCES navigation_ui_test.cpp LIBS navigation_ui m)
onc_test(localization_test SOURCES localization_test.cpp LIBS navigation_ui m)
onc_test(voice_prompts_test SOURCES voice_prompts_test.cpp LIBS navigation_ui m)
onc_test(voice_worker_test SOURCES voice_worker_test.cpp LIBS navigation_ui m)
onc_test(voice_link_test SOURCES voice_link_test.cpp LIBS navigation_ui m rt)
onc_test(process_focus_test SOURCES process_focus_test.cpp LIBS voice_daemon navigation_ui m)
onc_test(camera_controller_test SOURCES camera_controller_test.cpp LIBS navigation_ui)
onc_test(dev_menu_test SOURCES dev_menu_test.cpp LIBS navigation_ui)
onc_test(reroute_state_test SOURCES reroute_state_test.cpp INCLUDES ${ONC_ROOT}/backends/osmand)
onc_test(labels_test SOURCES labels_test.cpp LIBS navigation_ui)
onc_test(route_check_test SOURCES route_check_test.cpp LIBS navigation_ui m)
# Address search index: built from a small dump fixture by the real builder.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_search_fixture ${CMAKE_CURRENT_BINARY_DIR}/search_fixture.idx)
add_custom_command(OUTPUT ${_search_fixture}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/search_proto/build_index.py
          ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/search_index.tsv ${_search_fixture}
  DEPENDS search_proto/build_index.py fixtures/search_index.tsv VERBATIM)
add_custom_target(search_fixture DEPENDS ${_search_fixture})
onc_test(search_index_test SOURCES search_index_test.cpp LIBS navigation_ui m
         DEFINES SEARCH_FIXTURE="${_search_fixture}")
add_dependencies(search_index_test search_fixture)
# Contraction-hierarchy routing: query, contraction, sidecar file and snap lookup
# (the library the navigation worker uses; pure C++, no OsmAnd).
onc_test(ch_test SOURCES ch_test.cpp INCLUDES ${ONC_ROOT}/backends/osmand/ch ${ONC_ROOT}/tools/ch)
# The vector style/tessellator test needs earcut.hpp from the OsmAnd tree.
if(NOT OSMAND_QNX_ROOT AND DEFINED ENV{OSMAND_QNX_ROOT})
  set(OSMAND_QNX_ROOT $ENV{OSMAND_QNX_ROOT})
endif()
if(NOT OSMAND_QNX_ROOT)
  get_filename_component(OSMAND_QNX_ROOT ${ONC_ROOT}/../osmand-qnx-build ABSOLUTE)
endif()
if(EXISTS ${OSMAND_QNX_ROOT}/source/core/include/mapbox/earcut.hpp)
  onc_test(vector_test SOURCES ${ONC_ROOT}/backends/osmand/vector/vector_style.cpp
           ${ONC_ROOT}/backends/osmand/vector/vector_tessellator.cpp vector_test.cpp
           INCLUDES ${ONC_ROOT}/backends/osmand ${OSMAND_QNX_ROOT}/source/core/include)
else()
  message(STATUS "vector_test skipped: no earcut.hpp (set OSMAND_QNX_ROOT)")
endif()

# UI renders for visual checks: ui_render_tool <out-dir>.
add_executable(ui_render_tool ui_render_tool.cpp)
target_link_libraries(ui_render_tool PRIVATE navigation_ui m)
target_compile_options(ui_render_tool PRIVATE ${ONC_WARNINGS})

# Contract and source checks.
set(C ${ONC_ROOT}/contracts/qr39)
set(MATRIX ${C}/observations)
foreach(check
    "portable_paths|${Python3_EXECUTABLE} -m tests.check_portable_paths"
    "symbol_names|${Python3_EXECUTABLE} -m tests.check_symbol_names"
    "plugin_elf_selftest|sh tests/check_plugin_elf.sh --self-test"
    "callback_coverage|sh tests/check_callback_coverage.sh ${MATRIX}/callback-api-matrix.csv ${C}/callbacks/runtime-coverage.csv src/compat/qr39/navicore_compat.c"
    "static_callback_additions|${Python3_EXECUTABLE} -m tests.check_static_callback_additions ${C}/callbacks/runtime-coverage.csv ${C}/callbacks/dispatch-additions.csv"
    "static_callback_contracts|${Python3_EXECUTABLE} -m tests.check_static_callback_contracts ${C}/callbacks/static-contracts.csv ${C}/callbacks/runtime-coverage.csv ${C}/callbacks/dispatch-additions.csv src/include/host_callbacks.h src/compat/qr39/host_callbacks.c src/compat/qr39/navicore_compat.c"
    "provider_coverage|${Python3_EXECUTABLE} -m tests.check_provider_coverage ${MATRIX}/provider-api-matrix.csv ${MATRIX}/pointer-api-matrix.csv ${C}/provider/api-coverage.csv src/compat/qr39/navicore_compat.c"
    "static_provider_slots|${Python3_EXECUTABLE} -m tests.check_static_provider_slots ${C}/provider/slot-manifest.csv src/compat/qr39/navicore_compat.c src/compat/qr39/provider_slots.def")
  string(FIND "${check}" "|" bar)
  string(SUBSTRING "${check}" 0 ${bar} name)
  math(EXPR start "${bar} + 1")
  string(SUBSTRING "${check}" ${start} -1 command)
  separate_arguments(command UNIX_COMMAND "${command}")
  add_test(NAME check_${name} COMMAND ${command} WORKING_DIRECTORY ${ONC_ROOT})
endforeach()
