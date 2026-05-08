# Parses docs/ScriptFunctions.txt (lines of `0x.text:HEXOFFSET name`) into
# a generated header containing a constexpr table of {name, offset} entries.
function(generate_script_functions_header INPUT_FILE OUTPUT_FILE)
    if(NOT EXISTS "${INPUT_FILE}")
        message(FATAL_ERROR "ScriptFunctions input not found: ${INPUT_FILE}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${INPUT_FILE}")

    file(STRINGS "${INPUT_FILE}" _SF_LINES)

    set(_SF_ENTRIES "")
    set(_SF_COUNT 0)
    foreach(_LINE IN LISTS _SF_LINES)
        if(_LINE MATCHES "^0x\\.text:([0-9A-Fa-f]+)[ \t]+(.+)$")
            set(_OFF "${CMAKE_MATCH_1}")
            set(_NAME "${CMAKE_MATCH_2}")
            string(STRIP "${_NAME}" _NAME)
            string(APPEND _SF_ENTRIES "    {\"${_NAME}\", 0x${_OFF}},\n")
            math(EXPR _SF_COUNT "${_SF_COUNT} + 1")
        endif()
    endforeach()

    set(_SF_HEADER [=[// Auto-generated from docs/ScriptFunctions.txt — do not edit.
#pragma once

#include <stdint.h>
#include <stddef.h>

struct ScriptFunctionEntry { const char* name; uintptr_t offset; };

inline constexpr ScriptFunctionEntry kScriptFunctions[] = {
@_SF_ENTRIES@};

inline constexpr size_t kScriptFunctionCount =
    sizeof(kScriptFunctions) / sizeof(kScriptFunctions[0]);
]=])

    string(CONFIGURE "${_SF_HEADER}" _SF_OUTPUT @ONLY)

    get_filename_component(_OUT_DIR "${OUTPUT_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_OUT_DIR}")
    file(WRITE "${OUTPUT_FILE}" "${_SF_OUTPUT}")

    message(STATUS "ScriptFunctions: wrote ${_SF_COUNT} entries to ${OUTPUT_FILE}")
endfunction()
