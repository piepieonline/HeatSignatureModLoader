project(SampleMod LANGUAGES CXX)

add_library(SampleMod SHARED
    SampleMod.cpp
)

target_compile_features(SampleMod PRIVATE cxx_std_17)

target_include_directories(SampleMod PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
)

find_package(imgui CONFIG REQUIRED)

target_link_libraries(SampleMod PRIVATE
    imgui::imgui
)

set_target_properties(SampleMod PROPERTIES
    OUTPUT_NAME "SampleMod"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET SampleMod POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SampleMod>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:SampleMod>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. SampleMod.dll will not be copied automatically.")
endif()
