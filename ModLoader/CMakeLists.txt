project(ModLoader LANGUAGES CXX)

file(GLOB_RECURSE ModLoader_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(ModLoader SHARED ${ModLoader_SOURCES})

find_package(minhook CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

find_package(Lua REQUIRED)
find_path(LUABRIDGE_INCLUDE_DIRS "LuaBridge/Array.h")
target_include_directories(ModLoader PRIVATE ${LUABRIDGE_INCLUDE_DIRS})

target_include_directories(ModLoader PRIVATE ${SCRIPT_FUNCTIONS_GENERATED_DIR})

target_compile_features(ModLoader PRIVATE cxx_std_17)
target_link_libraries(ModLoader PRIVATE
    ${LUA_LIBRARIES}
    minhook::minhook
    imgui::imgui
    nlohmann_json::nlohmann_json
    d3d9.lib)

set_target_properties(ModLoader PROPERTIES
  OUTPUT_NAME "ModLoader"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(HEATSIG_DIR)
    add_custom_command(TARGET ModLoader POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ModLoader>
            "${HEATSIG_DIR}/$<TARGET_FILE_NAME:ModLoader>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. Built DLL will not be copied automatically.")
endif()
