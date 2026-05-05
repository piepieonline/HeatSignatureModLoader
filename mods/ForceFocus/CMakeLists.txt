project(ForceFocus LANGUAGES CXX)

add_library(ForceFocus SHARED ForceFocus.cpp)

target_compile_features(ForceFocus PRIVATE cxx_std_17)

target_include_directories(ForceFocus PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
)

set_target_properties(ForceFocus PROPERTIES
    OUTPUT_NAME "ForceFocus"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET ForceFocus POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ForceFocus>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:ForceFocus>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. ForceFocus.dll will not be copied automatically.")
endif()
