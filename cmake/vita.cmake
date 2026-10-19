# Packaging helpers for Vita targets.
#
# TITLE_ID must be 4 letters + 5 digits: `BR0000001` is invalid and
# vita_create_vpk rejects it.
#
# The TITLE_ID is pinned (BSHT00016): one bubble, replaced by each install.
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

# LiveArea art: the key art and the shell logo (Mike Klubnika's blessing).
macro(_bsht_livearea)
    set(_livearea "${CMAKE_SOURCE_DIR}/assets/livearea")
    foreach(_art "ICON0.PNG" "bg.png" "startup.png" "template.xml")
        if(NOT EXISTS "${_livearea}/${_art}")
            message(FATAL_ERROR
                "assets/livearea/${_art} is missing.
"
                "  The bubble would ship blank and unidentifiable, which is how
"
                "  the wrong build got launched once already.
"
                "  Run: python tools/make_livearea.py")
        endif()
    endforeach()

    # Every sce_sys image must be an 8-bit palettised PNG (colour type 3).
    #
    # Not 8 bits per channel: an indexed palette, what pngquant produces. A
    # 32-bit RGBA PNG passes every ordinary check (right size, valid PNG, not
    # interlaced) and is then rejected by the promoter after the package has
    # copied, so the install dies at 98% with 0x8010113D and reads like a
    # corrupt transfer rather than a format error.
    #
    # Byte 25 of a PNG is the IHDR colour type: 8 signature + 4 length +
    # 4 chunk type + 4 width + 4 height + 1 bit depth.
    foreach(_img "ICON0.PNG" "bg.png" "startup.png")
        file(READ "${_livearea}/${_img}" _colour_type OFFSET 25 LIMIT 1 HEX)
        if(NOT _colour_type STREQUAL "03")
            message(FATAL_ERROR
                "assets/livearea/${_img} is PNG colour type 0x${_colour_type}, "
                "needs 03 (palette).
"
                "  The Vita promoter rejects anything else: the install reaches "
                "98% and fails
  with 0x8010113D.
"
                "  Run: python tools/make_livearea.py")
        endif()
    endforeach()
    set(BSHT_LIVEAREA_FILES
        FILE "${_livearea}/ICON0.PNG"   sce_sys/icon0.png
        FILE "${_livearea}/bg.png"      sce_sys/livearea/contents/bg.png
        FILE "${_livearea}/startup.png" sce_sys/livearea/contents/startup.png
        FILE "${_livearea}/template.xml" sce_sys/livearea/contents/template.xml)
endmacro()

function(_bsht_check_title_id title_id)
    string(LENGTH "${title_id}" _len)
    if(NOT _len EQUAL 9)
        message(FATAL_ERROR
            "TITLE_ID '${title_id}' must be exactly 9 chars (4 letters + 5 digits)")
    endif()
endfunction()

# The code-only vpk: the binary, the LiveArea art and the port's own files.
# The game data comes from bsht-extract's game folder, copied to
# ux0:data/bsht/game/; tests/check_vpk_no_game_data.cmake keeps it that way.
function(vita_package target title_id title_name)
    _bsht_check_title_id("${title_id}")
    _bsht_livearea()

    set(_logo "${CMAKE_SOURCE_DIR}/assets/baked/menu_logo.rgba")
    if(NOT EXISTS "${_logo}")
        message(FATAL_ERROR "assets/baked/menu_logo.rgba is missing: python tools/bake_menu_logo.py")
    endif()

    vita_create_self(${target}.self ${target} UNSAFE)
    vita_create_vpk(${target}.vpk ${title_id} ${target}.self
        VERSION "01.00"
        NAME "${title_name}"
        FILE "${_logo}" data/menu_logo.rgba
        FILE "${CMAKE_SOURCE_DIR}/tools/achievements_en.json" data/achievements_en.json
        # The licences of what the binary is built from (THIRD_PARTY.md).
        FILE "${CMAKE_SOURCE_DIR}/LICENSE" data/licenses/LICENSE.txt
        FILE "${CMAKE_SOURCE_DIR}/THIRD_PARTY.md" data/licenses/THIRD_PARTY.md
        FILE "${CMAKE_SOURCE_DIR}/licenses/lgpl-3.0.txt" data/licenses/lgpl-3.0.txt
        FILE "${CMAKE_SOURCE_DIR}/licenses/sdl2.txt" data/licenses/sdl2.txt
        FILE "${CMAKE_SOURCE_DIR}/licenses/math-neon.txt" data/licenses/math-neon.txt
        FILE "${CMAKE_SOURCE_DIR}/licenses/godot-engine.txt" data/licenses/godot-engine.txt
        FILE "${CMAKE_SOURCE_DIR}/licenses/pcg.txt" data/licenses/pcg.txt
        FILE "${CMAKE_SOURCE_DIR}/licenses/apache-2.0.txt" data/licenses/apache-2.0.txt
        FILE "${CMAKE_SOURCE_DIR}/third_party/libogg/LICENSE" data/licenses/libogg.txt
        FILE "${CMAKE_SOURCE_DIR}/third_party/font8x8/LICENSE" data/licenses/font8x8.txt
        ${BSHT_LIVEAREA_FILES})
endfunction()
