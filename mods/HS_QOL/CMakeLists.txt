project(HS_QOL LANGUAGES CXX)

add_library(HS_QOL SHARED
    HS_QOL.cpp
    ForceFocus.cpp
    ForceFocus.h
    FixCursorLock.cpp
    FixCursorLock.h
    LoudGunPrefix.cpp
    LoudGunPrefix.h
)

target_compile_features(HS_QOL PRIVATE cxx_std_17)

target_include_directories(HS_QOL PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
)

set_target_properties(HS_QOL PROPERTIES
    OUTPUT_NAME "HS_QOL"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET HS_QOL POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:HS_QOL>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:HS_QOL>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. HS_QOL.dll will not be copied automatically.")
endif()
