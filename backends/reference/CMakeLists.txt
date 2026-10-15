# Reference backend plugin (SDK sample, and the plugin the provider's tests
# load); the outbound-test build also exercises the V3 outbound services.
foreach(variant reference outbound_test)
  if(variant STREQUAL "reference")
    set(name MockNavBackendReference)
  else()
    set(name MockNavBackendOutboundTest)
  endif()
  add_library(${variant}_backend SHARED reference_backend.c)
  target_include_directories(${variant}_backend PRIVATE ${ONC_ROOT}/include)
  target_compile_options(${variant}_backend PRIVATE ${ONC_WARNINGS} ${ONC_TARGET_FLAGS}
                         ${ONC_THUMB} -fvisibility=hidden)
  if(variant STREQUAL "outbound_test")
    target_compile_definitions(${variant}_backend PRIVATE MOCK_REF_PLUGIN_OUTBOUND_TEST=1)
  endif()
  set_target_properties(${variant}_backend PROPERTIES OUTPUT_NAME ${name}
                        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  target_link_options(${variant}_backend PRIVATE -Wl,-soname,lib${name}.so
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/reference_backend.map
    -Wl,--no-undefined -Wl,--gc-sections -static-libgcc)
  target_link_libraries(${variant}_backend PRIVATE GLESv2 c)
  add_custom_command(TARGET ${variant}_backend POST_BUILD
    COMMAND ${QNX_ENV} READELF=${QNX_READELF} sh ${ONC_ROOT}/tests/check_plugin_elf.sh
            $<TARGET_FILE:${variant}_backend>
    VERBATIM)
endforeach()
