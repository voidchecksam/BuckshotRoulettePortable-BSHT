# The renderer must decide whether a prop is drawn through ONE rule.
#
# WHY THIS IS A MECHANICAL CHECK AND NOT A UNIT TEST
#
# The first bug was an ORDERING: SceneRenderer skipped any prop hidden at scene
# load BEFORE it resolved that prop's animation target, so an animation could
# never show a prop the scene had hidden. 113 of the 142 animated nodes start
# hidden, so nearly everything that moves was unreachable. Later ones were
# missing state: layers and transparency were never read, so the crushed dealer
# head and the waiver highlight were drawn over everything.
#
# A unit test cannot reach SceneRenderer: it needs a GL context. What CAN be
# checked is that the renderer expresses the rule exactly once, by calling
# render::PropIsDrawn (render/prop_visibility.h, unit-tested), and never tests
# the pieces of Godot state that rule combines. With one expression of the rule
# a partial copy has nowhere to live.

set(_renderer "${SRC}/render/scene_renderer.cpp")
if(NOT EXISTS "${_renderer}")
    message(FATAL_ERROR "scene_renderer.cpp not found at ${_renderer}")
endif()

file(READ "${_renderer}" _text)

# 1. The rule must actually be used.
if(NOT _text MATCHES "PropIsDrawn")
    message(FATAL_ERROR
        "scene_renderer.cpp does not call PropIsDrawn.\n"
        "  Whether a prop is drawn is one rule: render/prop_visibility.h.")
endif()

# 2. No second expression of it: the renderer may not test the pieces itself.
set(_bad_patterns
    "is_visible_in_tree[ \t]*\\("
    "get_layers[ \t]*\\("
    "get_transparency[ \t]*\\("
    "get_draw_alpha[ \t]*\\("
    "visibleAtLoad")

foreach(_pat IN LISTS _bad_patterns)
    if(_text MATCHES "${_pat}")
        message(FATAL_ERROR
            "scene_renderer.cpp tests visibility state directly.\n"
            "  Matched: ${_pat}\n"
            "  Visibility, layers and transparency are read ONLY by PropIsDrawn.")
    endif()
endforeach()

message(STATUS "prop drawing goes through PropIsDrawn")
