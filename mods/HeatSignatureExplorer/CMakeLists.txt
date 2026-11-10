project(HeatSignatureExplorer LANGUAGES CXX)

add_library(HeatSignatureExplorer SHARED
    HeatSignatureExplorer.cpp
)

target_compile_features(HeatSignatureExplorer PRIVATE cxx_std_17)

target_include_directories(HeatSignatureExplorer PRIVATE
    ${CMAKE_SOURCE_DIR}/ModLoader
)

find_package(imgui CONFIG REQUIRED)

target_link_libraries(HeatSignatureExplorer PRIVATE
    imgui::imgui
)

set_target_properties(HeatSignatureExplorer PROPERTIES
    OUTPUT_NAME "HeatSignatureExplorer"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET HeatSignatureExplorer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:HeatSignatureExplorer>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:HeatSignatureExplorer>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. HeatSignatureExplorer.dll will not be copied automatically.")
endif()
