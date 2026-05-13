project(d3d9_proxy LANGUAGES CXX)

add_library(d3d9_proxy SHARED d3d9.cpp)

target_compile_definitions(d3d9_proxy PRIVATE UNICODE _UNICODE)
target_include_directories(d3d9_proxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(d3d9_proxy PRIVATE d3d9.lib)

# Build output name exactly "d3d9.dll"
set_target_properties(d3d9_proxy PROPERTIES
  OUTPUT_NAME "d3d9"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy DLL to game
if(HEATSIG_DIR)
    add_custom_command(TARGET d3d9_proxy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:d3d9_proxy>
            "${HEATSIG_DIR}/$<TARGET_FILE_NAME:d3d9_proxy>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. Built DLL will not be copied automatically.")
endif()
