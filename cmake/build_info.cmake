# cmake -DOUT=<header> -DREPO=<checkout> -P build_info.cmake
# Writes the git description (commit, -dirty) as OPEN_NAVICORE_GIT, touching
# OUT only when it changes so a new commit rebuilds one object.
execute_process(COMMAND git describe --always --dirty --abbrev=12 --exclude=*
                WORKING_DIRECTORY ${REPO} OUTPUT_VARIABLE describe
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR describe STREQUAL "")
  set(describe unknown)
endif()
set(content "#define OPEN_NAVICORE_GIT \"${describe}\"\n")
if(EXISTS ${OUT})
  file(READ ${OUT} old)
endif()
if(NOT old STREQUAL content)
  file(WRITE ${OUT} "${content}")
endif()
