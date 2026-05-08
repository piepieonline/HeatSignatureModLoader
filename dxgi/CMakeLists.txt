project(dxgi LANGUAGES CXX)

add_library(dxgi SHARED dxgi.cpp)

target_compile_definitions(dxgi PRIVATE UNICODE _UNICODE)
target_include_directories(dxgi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Build output name exactly "dinput8.dll"
set_target_properties(dxgi PROPERTIES
  OUTPUT_NAME "dxgi"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy DLL to game
if(HEATSIG_DIR)
    add_custom_command(TARGET dxgi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:dxgi>
            "${HEATSIG_DIR}/$<TARGET_FILE_NAME:dxgi>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. Built DLL will not be copied automatically.")
endif()