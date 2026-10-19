# scripts_mirror_gdscript: every translation mirrors its GDScript.
#
# The rule is 1:1: scripts/X.gd -> src/scripts/X.cpp, same function names. A
# function dropped quietly is how an invented layer starts, so a translation
# may only leave one out by saying so in its header.
#
# GD_DIR is the game's scripts folder: the reference game's (BSHT_REFERENCE_GAME),
# or the negative fixture's. Without it the test is skipped.
#
# Fails when, for any src/scripts/X.cpp (generated bind_support/bindings aside):
#   - GD_DIR/X.gd does not exist;
#   - src/scripts/X.h is missing or lacks its "// source: scripts/X.gd" block;
#   - a `func F` in X.gd has no `::F(` in X.cpp and no
#     "// PORT: omitted F (" line in X.h.
#
# Negative-tested by scripts_mirror_gdscript_rejects_missing_func against
# tests/fixtures/scripts_mirror_bad.

if(NOT ROOT)
    message(FATAL_ERROR "check_scripts_mirror: pass -DROOT=<repo root>")
endif()

if(NOT GD_DIR OR NOT EXISTS "${GD_DIR}")
    message(STATUS "no game scripts at ${GD_DIR}: make the reference game first (bsht-extract --unpack-to)")
    cmake_language(EXIT 77)
endif()

file(GLOB _cpps "${ROOT}/src/scripts/*.cpp")
set(_errors "")
set(_checked 0)

foreach(_cpp IN LISTS _cpps)
    get_filename_component(_stem "${_cpp}" NAME_WE)
    if(_stem STREQUAL "bind_support" OR _stem STREQUAL "bindings")
        continue()
    endif()
    math(EXPR _checked "${_checked} + 1")
    set(_gd "${GD_DIR}/${_stem}.gd")
    set(_h "${ROOT}/src/scripts/${_stem}.h")
    if(NOT EXISTS "${_gd}")
        list(APPEND _errors "src/scripts/${_stem}.cpp has no ${GD_DIR}/${_stem}.gd")
        continue()
    endif()
    if(NOT EXISTS "${_h}")
        list(APPEND _errors "src/scripts/${_stem}.cpp has no header src/scripts/${_stem}.h")
        continue()
    endif()
    file(READ "${_gd}" _gdtext)
    file(READ "${_h}" _htext)
    file(READ "${_cpp}" _ctext)
    string(FIND "${_htext}" "// source: scripts/${_stem}.gd" _src)
    if(_src EQUAL -1)
        list(APPEND _errors "src/scripts/${_stem}.h lacks its header block '// source: scripts/${_stem}.gd'")
    endif()
    string(REGEX MATCHALL "(^|\n)func[ \t]+[A-Za-z_0-9]+" _funcs "${_gdtext}")
    foreach(_f IN LISTS _funcs)
        string(REGEX REPLACE "(^|\n)func[ \t]+" "" _name "${_f}")
        string(FIND "${_ctext}" "::${_name}(" _impl)
        string(FIND "${_htext}" "// PORT: omitted ${_name} (" _omit)
        if(_impl EQUAL -1 AND _omit EQUAL -1)
            list(APPEND _errors
                "${_stem}: func ${_name} is neither translated (::${_name}( in src/scripts/${_stem}.cpp) "
                "nor listed as '// PORT: omitted ${_name} (<phase>)' in src/scripts/${_stem}.h")
        endif()
    endforeach()
endforeach()

if(_errors)
    foreach(_e IN LISTS _errors)
        message(SEND_ERROR "${_e}")
    endforeach()
    message(FATAL_ERROR "translated scripts must mirror their GDScript (same names, one function per func)")
endif()
message(STATUS "checked ${_checked} translated scripts against ${GD_DIR}")
