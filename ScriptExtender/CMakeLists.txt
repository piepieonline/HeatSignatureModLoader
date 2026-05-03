project(ScriptExtender LANGUAGES CXX)

file(GLOB_RECURSE ScriptExtender_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(ScriptExtender SHARED ${ScriptExtender_SOURCES})

find_package(minhook CONFIG REQUIRED)

find_package(Lua REQUIRED)
find_path(LUABRIDGE_INCLUDE_DIRS "LuaBridge/Array.h")
target_include_directories(ScriptExtender PRIVATE ${LUABRIDGE_INCLUDE_DIRS})

target_compile_features(ScriptExtender PRIVATE cxx_std_17)
target_link_libraries(ScriptExtender PRIVATE ${LUA_LIBRARIES} minhook::minhook)

set_target_properties(ScriptExtender PROPERTIES
  OUTPUT_NAME "ScriptExtender"
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(HEATSIG_DIR)
    add_custom_command(TARGET ScriptExtender POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ScriptExtender>
            "${HEATSIG_DIR}/$<TARGET_FILE_NAME:ScriptExtender>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. Built DLL will not be copied automatically.")
endif()
