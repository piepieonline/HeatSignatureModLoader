project(ScreenRecorder LANGUAGES CXX)

# We force /O2 in Debug below; CMake's default Debug flags include /RTC1 which
# is incompatible with optimization, so strip it from this directory's scope.
string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

add_library(ScreenRecorder SHARED ScreenRecorder.cpp)

# DXCam-CPP requires C++20.
target_compile_features(ScreenRecorder PRIVATE cxx_std_20)

# Always build this mod as Release-equivalent, even when the parent solution
# is configured Debug. Reasons:
#   * vcpkg's debug static libs (opencv4 / DXCam) are built /MTd and emit
#     conflicting CRT + iterator-debug + ASAN-annotation pragmas, which is
#     impossible to reconcile with our default /MDd build.
#   * Recording video has no debugger value here -- we never need to step
#     through ScreenRecorder.cpp under Debug config.
# Forcing /MT + mapping imported targets' Debug config to Release lines up
# every detect_mismatch pragma at the "release" value and links the right libs.
set_target_properties(ScreenRecorder PROPERTIES
    MSVC_RUNTIME_LIBRARY          "MultiThreaded"
    MSVC_DEBUG_INFORMATION_FORMAT ""
)
target_compile_options(ScreenRecorder PRIVATE
    $<$<CONFIG:Debug>:/O2 /Ob2>
)
target_compile_definitions(ScreenRecorder PRIVATE
    $<$<CONFIG:Debug>:NDEBUG>
)

# MAP_IMPORTED_CONFIG_<CONFIG> must be set on the *imported* targets, not on
# the consumer. The CMAKE_MAP_IMPORTED_CONFIG_<CONFIG> variable initialises
# that property on every imported target created after it is set, so do it
# before find_package() so DXCam, OpenCV, and their transitive deps all inherit
# it. Scope is this directory only (set() is directory-scoped in CMake).
set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG          Release "")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL     Release "")
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release "")

target_include_directories(ScreenRecorder PRIVATE
    ${CMAKE_SOURCE_DIR}/ScriptExtender
)

find_package(DXCam CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

target_link_libraries(ScreenRecorder PRIVATE
    DXCam::DXCam
    ${OpenCV_LIBS}
    imgui::imgui
    mf.lib
    mfplat.lib
    mfreadwrite.lib
    mfuuid.lib
    d3d11.lib
    dxgi.lib
    windowscodecs.lib
    Propsys.lib
    delayimp.lib
)

target_link_options(ScreenRecorder PRIVATE
    /DELAYLOAD:mf.dll
    /DELAYLOAD:mfplat.dll
    /DELAYLOAD:mfreadwrite.dll
    /DELAYLOAD:windowscodecs.dll
    /DELAYLOAD:propsys.dll
    /DELAYLOAD:evr.dll
)

set_target_properties(ScreenRecorder PROPERTIES
    OUTPUT_NAME "ScreenRecorder"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/mods
)

if(HEATSIG_DIR)
    add_custom_command(TARGET ScreenRecorder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HEATSIG_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ScreenRecorder>
            "${HEATSIG_DIR}/mods/$<TARGET_FILE_NAME:ScreenRecorder>"
    )
else()
    message(WARNING "HEATSIG_DIR is not set. ScreenRecorder.dll will not be copied automatically.")
endif()
