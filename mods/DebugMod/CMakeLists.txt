project(DebugMod LANGUAGES CXX)

add_library(DebugMod SHARED
    DebugMod.cpp
    PropertyNames.h
)

target_compile_features(DebugMod PRIVATE cxx_std_17)

target_include_directories(DebugMod PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
)

set_target_properties(DebugMod PROPERTIES
    OUTPUT_NAME "DebugMod"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET DebugMod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:DebugMod>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:DebugMod>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. DebugMod.dll will not be copied automatically.")
endif()
