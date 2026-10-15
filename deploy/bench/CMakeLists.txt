# Bench-only helpers for the radio (not part of navigation.ifs).
add_executable(nv_navigation_launcher nv_navigation_launcher.c)
target_compile_options(nv_navigation_launcher PRIVATE ${ONC_WARNINGS} ${ONC_THUMB})
target_link_libraries(nv_navigation_launcher PRIVATE c)
set_target_properties(nv_navigation_launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bench)
