# The provider: libNaviCore.so, loaded by the stock nv_navigation in place of
# the native NaviCore.
set(ONC_PROVIDER_INCLUDES ${ONC_ROOT}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat/qr39
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime ${CMAKE_CURRENT_SOURCE_DIR}/backend
    ${CMAKE_CURRENT_SOURCE_DIR}/renderer PARENT_SCOPE)

add_library(provider SHARED
  compat/qr39/navicore_compat.c
  compat/qr39/host_callbacks.c
  runtime/open_navi_api_telemetry.c
  runtime/open_navi_callback_backend.c
  runtime/open_navi_cluster_backend.c
  runtime/open_navi_cluster_agent.c
  runtime/open_navi_cluster_relay.c
  ${ONC_CAGENT_SOURCES}
  runtime/open_navi_gps_backend.c
  runtime/open_navi_pointer_backend.c
  runtime/open_navi_scalar_backend.c
  backend/open_navi_backend_loader.c
  backend/open_navi_default_backend.c
  renderer/open_navi_renderer.c
  renderer/open_navi_renderer_backend_table.c
  renderer/open_navi_renderer_pointer.c
  renderer/open_navi_renderer_dispatch.c
  renderer/open_navi_screen_input.c
  platform/qnx/open_navi_renderer_qnx.c)
target_include_directories(provider PRIVATE ${ONC_ROOT}/include include compat/qr39 runtime backend
                           renderer ${ONC_CAGENT_INCLUDES})
target_compile_options(provider PRIVATE ${ONC_WARNINGS} ${ONC_TARGET_FLAGS} ${ONC_THUMB}
                       -fvisibility=hidden)
target_compile_definitions(provider PRIVATE _REENTRANT ${ONC_CAGENT_DEFINES})
set_target_properties(provider PROPERTIES OUTPUT_NAME NaviCore
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_options(provider PRIVATE -Wl,-soname,libNaviCore.so -Wl,--gc-sections
                    -Wl,--no-undefined -static-libgcc)
target_link_libraries(provider PRIVATE screen EGL GLESv2 socket c)
# The four PAS entry points are exported, nothing else (tests/check_elf.sh).
add_custom_command(TARGET provider POST_BUILD
  COMMAND ${QNX_ENV} READELF=${QNX_READELF} sh ${ONC_ROOT}/tests/check_elf.sh
          $<TARGET_FILE:provider>
  COMMAND ${QNX_ENV} OBJDUMP=${QNX_OBJDUMP} python3 ${ONC_ROOT}/tests/check_arm_abi.py
          $<TARGET_OBJECTS:provider>
  VERBATIM COMMAND_EXPAND_LISTS)
