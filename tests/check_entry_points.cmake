# The entry points hold the platform and nothing else.
#
# app::GameApp is the game both entry points run, so a feature is wired once.
# An entry point that constructs the game's own objects, or that grows past its
# budget, fails the build, so the duplication cannot grow back one addition at
# a time.

if(NOT DEFINED SRC OR NOT DEFINED MAX_LINES)
    message(FATAL_ERROR "usage: -DSRC=<src dir> -DMAX_LINES=<n> -P check_entry_points.cmake")
endif()

set(_forbidden
    "game::Session[ \t]+[a-z]"
    "render::SceneRenderer[ \t]+[a-z]"
    "render::Label3DRenderer[ \t]+[a-z]"
    "game::UiScreen[ \t]+[a-z]"
    "godot::SceneTree[ \t]+[a-z]"
    "scripts::BindScripts[ \t]*\\(")

foreach(_file main_desktop.cpp main_vita.cpp)
    set(_path "${SRC}/${_file}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "${_path} not found")
    endif()
    file(STRINGS "${_path}" _lines)
    list(LENGTH _lines _count)
    if(_count GREATER MAX_LINES)
        message(FATAL_ERROR
            "${_file} is ${_count} lines, over its budget of ${MAX_LINES}.\n"
            "  Game and presentation code belongs in app::GameApp so both platforms\n"
            "  get it. Only platform code belongs in an entry point.")
    endif()
    file(READ "${_path}" _text)
    foreach(_pat IN LISTS _forbidden)
        if(_text MATCHES "${_pat}")
            message(FATAL_ERROR
                "${_file} constructs a game object directly (matched: ${_pat}).\n"
                "  That is app::GameApp's job; wiring it here wires it for one platform.")
        endif()
    endforeach()
    message(STATUS "${_file}: ${_count} lines, platform only")
endforeach()
