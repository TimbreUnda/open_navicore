# QNX 6.6 armle-v7 with the modern GCC (8.5) of the OsmAnd QNX build, for the
# whole product (provider and backend). The QNX SDK supplies the sysroot,
# binutils and mkifs.
#
# Locations (first match): -D variable, environment variable, workspace
# default (the directory that holds this repository):
#   QNX_SDK          <workspace>/tools/qnx660-sdk
#   QNX_HOST_LIBS    <workspace>/tools/qnx-host-libs (host libraries the SDK
#                    binaries need)
#   OSMAND_QNX_ROOT  <workspace>/osmand-qnx-build (compiler, OsmAnd core, Qt)
# From a linked worktree the workspace default is wrong: set them.
get_filename_component(_onc_workspace "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(_var_default IN ITEMS "QNX_SDK|tools/qnx660-sdk" "QNX_HOST_LIBS|tools/qnx-host-libs"
                              "OSMAND_QNX_ROOT|osmand-qnx-build")
  string(REPLACE "|" ";" _pair "${_var_default}")
  list(GET _pair 0 _var)
  list(GET _pair 1 _default)
  if(NOT ${_var})
    if(DEFINED ENV{${_var}})
      set(${_var} "$ENV{${_var}}")
    else()
      set(${_var} "${_onc_workspace}/${_default}")
    endif()
  endif()
  # try_compile re-reads this file; the environment carries the values.
  set(ENV{${_var}} "${${_var}}")
endforeach()

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR armv7)
set(QNX_HOST ${QNX_SDK}/host/linux/x86)
set(QNX_TARGET ${QNX_SDK}/target/qnx6)
set(CMAKE_SYSROOT ${QNX_TARGET})

set(_gcc ${OSMAND_QNX_ROOT}/gcc-out/bin/arm-unknown-nto-qnx6.6.0eabi)
set(CMAKE_C_COMPILER ${_gcc}-gcc)
set(CMAKE_CXX_COMPILER ${_gcc}-g++)

# The compiler finds the SDK's assembler and linker through -B.
set(_binutils ${CMAKE_BINARY_DIR}/qnx-binutils)
file(MAKE_DIRECTORY ${_binutils})
foreach(_tool as ld)
  if(NOT EXISTS ${_binutils}/${_tool})
    file(CREATE_LINK ${QNX_HOST}/usr/bin/arm-unknown-nto-qnx6.6.0eabi-${_tool}
         ${_binutils}/${_tool} SYMBOLIC)
  endif()
endforeach()

# The QNX firmware's ABI: ARMv7, VFPv3-D16, soft-float calling convention.
set(_arch "-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=softfp -B${_binutils}/")
set(CMAKE_C_FLAGS_INIT "${_arch}")
set(CMAKE_CXX_FLAGS_INIT "${_arch}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_arch}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_arch}")

# The SDK binaries need these at run time.
set(ENV{QNX_HOST} ${QNX_HOST})
set(ENV{QNX_TARGET} ${QNX_TARGET})
set(ENV{LD_LIBRARY_PATH} ${QNX_HOST_LIBS})
set(QNX_ENV env QNX_HOST=${QNX_HOST} QNX_TARGET=${QNX_TARGET} LD_LIBRARY_PATH=${QNX_HOST_LIBS})
# ccache (when installed) runs inside env, so the cached compiler sees the
# SDK variables; set CCACHE_DISABLE=1 to bypass it.
find_program(ONC_CCACHE ccache)
set(_onc_ccache)
if(ONC_CCACHE)
  set(_onc_ccache ${ONC_CCACHE})
endif()
set(CMAKE_C_COMPILER_LAUNCHER ${QNX_ENV} ${_onc_ccache})
set(CMAKE_CXX_COMPILER_LAUNCHER ${QNX_ENV} ${_onc_ccache})
set(CMAKE_C_LINKER_LAUNCHER ${QNX_ENV})
set(CMAKE_CXX_LINKER_LAUNCHER ${QNX_ENV})
set(QNX_READELF ${QNX_HOST}/usr/bin/ntoarmv7-readelf)
set(QNX_OBJDUMP ${QNX_HOST}/usr/bin/ntoarmv7-objdump)

set(CMAKE_FIND_ROOT_PATH ${QNX_TARGET})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ifs-build (IfsPackage.cmake) runs the SDK's mkifs with these.
set(IFSBUILD_MKIFS ${QNX_HOST}/usr/bin/mkifs)
set(IFSBUILD_ENV QNX_HOST=${QNX_HOST} QNX_TARGET=${QNX_TARGET}
    LD_LIBRARY_PATH=${QNX_HOST_LIBS} PROCESSOR=armle-v7)
