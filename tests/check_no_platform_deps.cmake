# Fails if core/, seq/ or assets/ ever includes a platform header.
#
# This guarantee is what keeps the simulation testable headless and portable to
# arm-vita-eabi, so it is enforced by a test rather than left to discipline. The
# moment someone reaches for SDL from inside a game rule, the build says no.
#
# Invoked as: cmake -DSRC=<repo>/src -P check_no_platform_deps.cmake

file(GLOB_RECURSE SRCS
     "${SRC}/core/*.h" "${SRC}/core/*.cpp"
     "${SRC}/seq/*.h"  "${SRC}/seq/*.cpp"
     "${SRC}/assets/*.h" "${SRC}/assets/*.cpp"
     "${SRC}/game/*.h"   "${SRC}/game/*.cpp"
     "${SRC}/godot/*.h"  "${SRC}/godot/*.cpp"
     "${SRC}/scripts/*.h" "${SRC}/scripts/*.cpp")

if(NOT SRCS)
    message(FATAL_ERROR "no sources found under ${SRC}/core, ${SRC}/seq or ${SRC}/assets")
endif()

set(BANNED
    "SDL.h" "SDL2/" "SDL_"
    "GL/" "GLES" "gl.h" "vitaGL.h"
    "psp2/" "windows.h" "Windows.h")

set(VIOLATIONS "")
foreach(f ${SRCS})
    file(READ "${f}" content)
    foreach(b ${BANNED})
        # Match both #include <x> and #include "x".
        string(FIND "${content}" "include <${b}" hit_angle)
        string(FIND "${content}" "include \"${b}" hit_quote)
        if(NOT hit_angle EQUAL -1 OR NOT hit_quote EQUAL -1)
            list(APPEND VIOLATIONS "${f} includes banned platform header '${b}'")
        endif()
    endforeach()
endforeach()

list(LENGTH SRCS n_files)
if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(SEND_ERROR "${v}")
    endforeach()
    message(FATAL_ERROR
        "core/, seq/ and assets/ must stay platform-free so they remain "
        "testable headless and portable to the Vita")
endif()

message(STATUS "checked ${n_files} files: core/, seq/ and assets/ are free of platform headers")
