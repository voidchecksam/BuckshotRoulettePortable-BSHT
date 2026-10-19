# Fails if any of the game's prose is compiled into the binary as a literal.
#
# The port ships the engine; players supply the game files from a copy they
# own, the same model as DDLC's Ren'Py ports, devilutionX and OpenMW. A
# hardcoded line of dealer dialogue would turn the vpk into a redistribution of
# the game. As a test rather than a convention, typing a line of dialogue into
# a .cpp because it is quicker than a table lookup fails the build.
#
# Invoked as:
#   cmake -DSRC=<repo>/src -DASSETS=<repo>/assets/extracted -P check_no_game_content.cmake
#
# ---------------------------------------------------------------------------
# What counts as content, and what does not
#
# Only the words the player reads are guarded:
#
#   dialogue_en.json     messages.*                 (the dealer's lines)
#   scene_strings.json   introductionDialogues[]    (the nine-beat introduction)
#                        loadingDialogues[]         (the shell-loading lines)
#                        itemDescription            (the item text)
#
# Node paths, aliases and item IDs are not guarded, even though they are
# strings in the same files. They are structural identifiers ("home", "enemy",
# a prop's scene path), and the port must hardcode some of them to address its
# own data. Guarding those would make this test cry wolf until it was switched
# off.
# ---------------------------------------------------------------------------

if(NOT SRC OR NOT ASSETS)
    message(FATAL_ERROR "check_no_game_content: pass -DSRC=<src dir> -DASSETS=<extracted dir>")
endif()

# A fragment must be this long to be searched for. Below it, a match says
# nothing: "BLANK." and "%s" occur legitimately in code, and a guard that fires
# on them is a guard that gets deleted.
set(MIN_LEN 12)

set(PROSE "")

# --- dialogue_en.json: every message value --------------------------------
set(_dlg_path "${ASSETS}/dialogue_en.json")
if(NOT EXISTS "${_dlg_path}")
    # tests/CMakeLists.txt marks this line a skip: a fresh clone has no tables.
    message(STATUS "SKIP: needs ${_dlg_path} (bsht-extract output; docs/tools/bsht-extract.md)")
    return()
endif()
file(READ "${_dlg_path}" _dlg)
string(JSON _msgs GET "${_dlg}" "messages")
string(JSON _n LENGTH "${_msgs}")
if(_n GREATER 0)
    math(EXPR _last "${_n} - 1")
    foreach(i RANGE 0 ${_last})
        string(JSON _key MEMBER "${_msgs}" ${i})
        string(JSON _val GET "${_msgs}" "${_key}")
        # A value inside its own key ("PRESS ANY BUTTON" in the key "PRESS ANY
        # BUTTON REMAP") is found wherever a script names the key, which
        # tr("KEY") must do. The key is the reference, not the content.
        string(FIND "${_key}" "${_val}" _inKey)
        if(_inKey EQUAL -1)
            list(APPEND PROSE "${_val}")
        endif()
    endforeach()
endif()

# --- scene_strings.json: the prose fields only ----------------------------
set(_scn_path "${ASSETS}/scene_strings.json")
if(NOT EXISTS "${_scn_path}")
    message(FATAL_ERROR
        "check_no_game_content: ${_scn_path} is missing.\n"
        "  Run bsht-extract --keep-work, then copy .work/out/extracted and .work/out/baked into assets/ (docs/tools/bsht-extract.md)")
endif()
file(READ "${_scn_path}" _scn)
string(JSON _nodes GET "${_scn}" "nodes")
string(JSON _nn LENGTH "${_nodes}")
if(_nn GREATER 0)
    math(EXPR _nlast "${_nn} - 1")
    foreach(i RANGE 0 ${_nlast})
        string(JSON _path MEMBER "${_nodes}" ${i})
        string(JSON _node GET "${_nodes}" "${_path}")

        # Arrays of lines: the introduction and the shell-loading dialogue.
        foreach(field "introductionDialogues" "loadingDialogues")
            string(JSON _arr ERROR_VARIABLE _e GET "${_node}" "${field}")
            if(NOT _e)
                string(JSON _alen LENGTH "${_arr}")
                if(_alen GREATER 0)
                    math(EXPR _alast "${_alen} - 1")
                    foreach(j RANGE 0 ${_alast})
                        string(JSON _line GET "${_arr}" ${j})
                        list(APPEND PROSE "${_line}")
                    endforeach()
                endif()
            endif()
        endforeach()

        # Single strings.
        string(JSON _desc ERROR_VARIABLE _e2 GET "${_node}" "itemDescription")
        if(NOT _e2)
            list(APPEND PROSE "${_desc}")
        endif()
    endforeach()
endif()

# --- split on newlines and drop anything too short to be evidence ---------
#
# A two-line message cannot appear verbatim in C++ anyway: the source would
# write it with an escaped \n or as two concatenated literals. Matching per LINE
# catches both spellings; matching the whole string would catch neither.
set(FRAGMENTS "")
foreach(p IN LISTS PROSE)
    string(REPLACE "\n" ";" _lines "${p}")
    foreach(line IN LISTS _lines)
        string(STRIP "${line}" line)
        string(LENGTH "${line}" _len)
        if(_len GREATER_EQUAL MIN_LEN)
            list(APPEND FRAGMENTS "${line}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES FRAGMENTS)

list(LENGTH FRAGMENTS n_frags)
if(n_frags EQUAL 0)
    message(FATAL_ERROR
        "check_no_game_content: extracted NO prose to check for. The tables are "
        "present but empty or the wrong shape -- this guard would pass "
        "vacuously, which is worse than failing.")
endif()

# --- scan every source file ONCE ------------------------------------------
#
# Read per file, not per fragment. The other way round is 70 x 90 = 6,300 file
# reads for the same answer.
file(GLOB_RECURSE SRCS "${SRC}/*.h" "${SRC}/*.cpp")
if(NOT SRCS)
    message(FATAL_ERROR "check_no_game_content: no sources found under ${SRC}")
endif()

set(VIOLATIONS "")
foreach(f ${SRCS})
    file(READ "${f}" content)
    foreach(frag IN LISTS FRAGMENTS)
        string(FIND "${content}" "${frag}" hit)
        if(NOT hit EQUAL -1)
            file(RELATIVE_PATH rel "${SRC}" "${f}")
            list(APPEND VIOLATIONS "src/${rel}: \"${frag}\"")
        endif()
    endforeach()
endforeach()

list(LENGTH SRCS n_files)
if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(SEND_ERROR "${v}")
    endforeach()
    message(FATAL_ERROR
        "Game content is compiled into the binary.\n"
        "Read these strings from assets/extracted/ at runtime instead -- the port "
        "ships the ENGINE and the user supplies the game files.")
endif()

message(STATUS
    "checked ${n_files} files against ${n_frags} lines of game prose: "
    "no game content compiled into the binary")
