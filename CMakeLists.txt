cmake_minimum_required(VERSION 3.26)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(HeatSignatureModLoader)

include(${CMAKE_SOURCE_DIR}/cmake/GenerateScriptFunctions.cmake)
set(SCRIPT_FUNCTIONS_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
generate_script_functions_header(
    ${CMAKE_SOURCE_DIR}/docs/ScriptFunctions.txt
    ${SCRIPT_FUNCTIONS_GENERATED_DIR}/ScriptFunctions.generated.h
)

# ScreenRecorder needs ATL and the Media Foundation stack, which the
# cross-compile SDK does not ship, so it is MSVC-only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(_hs_screenrecorder_default ON)
else()
    set(_hs_screenrecorder_default OFF)
endif()
option(HS_BUILD_SCREENRECORDER "Build the ScreenRecorder mod" ${_hs_screenrecorder_default})

add_subdirectory(d3d9)
add_subdirectory(ModLoader)
if(HS_BUILD_SCREENRECORDER)
    add_subdirectory(mods/ScreenRecorder)
endif()
add_subdirectory(mods/DiscordRichPresence)
add_subdirectory(mods/HS_QOL)
add_subdirectory(mods/DebugMod)
add_subdirectory(mods/SampleMod)

set(MISC_FILES 
    README.md
    vcpkg.json
)
target_sources(ModLoader PRIVATE ${MISC_FILES})

# Run and debug setup for VS
if (CMAKE_GENERATOR MATCHES "Visual Studio")
    set(DEBUGGER_COMMAND "${HEATSIG_DIR}/Heat_Signature.exe")
    set(DEBUGGER_WORKDIR "${HEATSIG_DIR}")
    set(DEBUGGER_ARGS "")

    set(USER_FILES
        "${CMAKE_BINARY_DIR}/ALL_BUILD.vcxproj.user"
        "${CMAKE_BINARY_DIR}/ModLoader/ModLoader.vcxproj.user"
        "${CMAKE_BINARY_DIR}/dxgi/dxgi.vcxproj.user"
    )

    foreach(USER_FILE IN LISTS USER_FILES)
        configure_file(
            "${CMAKE_SOURCE_DIR}/cmake_templates/DebuggerAttach.vcxproj.user.template"
            "${USER_FILE}"
            @ONLY
        )
    endforeach()
endif()