# Cross-compile 32-bit Windows DLLs from Linux with clang-cl + lld-link against
# an MSVC CRT / Windows SDK unpacked by xwin (https://github.com/Jake-Shadle/xwin).
#
# Requires:
#   XWIN_SDK_DIR    xwin splat output (contains crt/ and sdk/), cache var or env
#   LLVM_TOOLS_DIR  optional, directory holding clang-cl / lld-link / llvm-lib
#
# See README.md "Building on Linux".

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT XWIN_SDK_DIR AND DEFINED ENV{XWIN_SDK_DIR})
    set(XWIN_SDK_DIR "$ENV{XWIN_SDK_DIR}" CACHE PATH "xwin splat output directory")
endif()
if(NOT XWIN_SDK_DIR)
    message(FATAL_ERROR "XWIN_SDK_DIR is not set. Point it at an xwin splat directory (see README).")
endif()
if(NOT EXISTS "${XWIN_SDK_DIR}/crt/include" OR NOT EXISTS "${XWIN_SDK_DIR}/sdk/include/um")
    message(FATAL_ERROR "XWIN_SDK_DIR '${XWIN_SDK_DIR}' does not look like an xwin splat directory.")
endif()

if(NOT LLVM_TOOLS_DIR AND DEFINED ENV{LLVM_TOOLS_DIR})
    set(LLVM_TOOLS_DIR "$ENV{LLVM_TOOLS_DIR}" CACHE PATH "Directory containing the LLVM tools")
endif()

set(_llvm_hints)
if(LLVM_TOOLS_DIR)
    list(APPEND _llvm_hints "${LLVM_TOOLS_DIR}")
endif()
file(GLOB _llvm_dirs "/usr/lib/llvm-*/bin")
list(SORT _llvm_dirs)
list(REVERSE _llvm_dirs)
list(APPEND _llvm_hints ${_llvm_dirs})

find_program(CMAKE_LINKER  NAMES lld-link HINTS ${_llvm_hints} REQUIRED)
find_program(CMAKE_AR      NAMES llvm-lib HINTS ${_llvm_hints} REQUIRED)
find_program(CMAKE_MT      NAMES llvm-mt  HINTS ${_llvm_hints})
find_program(CMAKE_RC_COMPILER NAMES llvm-rc HINTS ${_llvm_hints})

# Debian/Ubuntu's clang packages ship no clang-cl binary, only the clang driver,
# which selects MSVC mode from argv[0] -- so make one where it is missing.
find_program(_clang_cl NAMES clang-cl HINTS ${_llvm_hints})
if(NOT _clang_cl)
    find_program(_clang NAMES clang HINTS ${_llvm_hints} REQUIRED)
    get_filename_component(_clang_real "${_clang}" REALPATH)
    set(_shim_dir "${CMAKE_BINARY_DIR}/llvm-shim")
    file(MAKE_DIRECTORY "${_shim_dir}")
    file(CREATE_LINK "${_clang_real}" "${_shim_dir}/clang-cl" SYMBOLIC)
    set(_clang_cl "${_shim_dir}/clang-cl")
endif()

set(CMAKE_C_COMPILER   "${_clang_cl}")
set(CMAKE_CXX_COMPILER "${_clang_cl}")

set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${XWIN_SDK_DIR}/crt/include"
    "${XWIN_SDK_DIR}/sdk/include/ucrt"
    "${XWIN_SDK_DIR}/sdk/include/um"
    "${XWIN_SDK_DIR}/sdk/include/shared"
    "${XWIN_SDK_DIR}/sdk/include/winrt")
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES ${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES})

set(_target_flags "--target=i386-pc-windows-msvc -Wno-unused-command-line-argument")

# The MSVC STL hard-errors on clang below its supported floor; the mismatch is
# benign for C++17 and the macro is the vendor's documented opt-out.
execute_process(COMMAND "${_clang_cl}" --version OUTPUT_VARIABLE _clang_ver ERROR_QUIET)
if(_clang_ver MATCHES "clang version ([0-9]+)")
    if(CMAKE_MATCH_1 LESS 19)
        string(APPEND _target_flags " /D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH")
    endif()
endif()

set(CMAKE_C_FLAGS_INIT   "${_target_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_target_flags}")

set(_libpaths "/libpath:\"${XWIN_SDK_DIR}/crt/lib/x86\" /libpath:\"${XWIN_SDK_DIR}/sdk/lib/um/x86\" /libpath:\"${XWIN_SDK_DIR}/sdk/lib/ucrt/x86\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "/machine:x86 ${_libpaths}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/machine:x86 ${_libpaths}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "/machine:x86 ${_libpaths}")
set(CMAKE_STATIC_LINKER_FLAGS_INIT "/machine:x86")

# A chainloaded toolchain replaces vcpkg's own, so matching the triplet's
# static CRT linkage is this file's job -- CMake would otherwise default to /MD.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
