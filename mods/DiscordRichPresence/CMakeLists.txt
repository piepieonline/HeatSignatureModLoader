project(DiscordRichPresence LANGUAGES CXX)

add_library(DiscordRichPresence SHARED DiscordRichPresence.cpp)

target_compile_features(DiscordRichPresence PRIVATE cxx_std_17)

# Force static Release CRT (/MT) in all configs so it matches the Release
# discord-rpc static lib (the debug lib uses /MTd, which we don't link).
set_target_properties(DiscordRichPresence PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

target_include_directories(DiscordRichPresence PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
)

# discord-rpc ships no CMake config; link the Release static lib directly.
# The debug lib is /MTd which would conflict, so always use the Release one.
target_link_libraries(DiscordRichPresence PRIVATE
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/discord-rpc.lib
)

set_target_properties(DiscordRichPresence PROPERTIES
    OUTPUT_NAME "DiscordRichPresence"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET DiscordRichPresence POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:DiscordRichPresence>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:DiscordRichPresence>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. DiscordRichPresence.dll will not be copied automatically.")
endif()
