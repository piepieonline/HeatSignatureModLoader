cmake_minimum_required(VERSION 3.26)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(HeatSignatureScriptExtender)

add_subdirectory(dinput8)
add_subdirectory(ScriptExtender)
add_subdirectory(mods/ScreenRecorder)
add_subdirectory(mods/DiscordRichPresence)

set(MISC_FILES 
    README.md
    vcpkg.json
)
target_sources(ScriptExtender PRIVATE ${MISC_FILES})

# Run and debug setup for VS
if (WIN32)
    set(DEBUGGER_COMMAND "${HEATSIG_DIR}/Heat_Signature.exe")
    set(DEBUGGER_WORKDIR "${HEATSIG_DIR}")
    set(DEBUGGER_ARGS "")

    set(USER_FILES
        "${CMAKE_BINARY_DIR}/ALL_BUILD.vcxproj.user"
        "${CMAKE_BINARY_DIR}/ScriptExtender/ScriptExtender.vcxproj.user"
        "${CMAKE_BINARY_DIR}/dinput8/dxgi.vcxproj.user"
    )

    foreach(USER_FILE IN LISTS USER_FILES)
        configure_file(
            "${CMAKE_SOURCE_DIR}/cmake_templates/DebuggerAttach.vcxproj.user.template"
            "${USER_FILE}"
            @ONLY
        )
    endforeach()
endif()