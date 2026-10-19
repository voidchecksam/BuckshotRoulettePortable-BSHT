# The vpk carries no game data: only the LiveArea art and the port's own
# files. The game comes from bsht-extract's game folder.
#
#   cmake -DROOT=<source dir> [-DVITA_CMAKE=<fixture>] -P check_vpk_no_game_data.cmake
if(NOT ROOT)
    message(FATAL_ERROR "usage: cmake -DROOT=<source dir> -P check_vpk_no_game_data.cmake")
endif()
set(_vita "${ROOT}/cmake/vita.cmake")
if(DEFINED VITA_CMAKE)
    set(_vita "${VITA_CMAKE}")   # the negative test points this at a fixture
endif()
file(READ "${_vita}" _text)

# A FILE source may be quoted or not; the destination is what lands in the vpk.
string(REGEX MATCHALL "FILE[ \t]+(\"[^\"]+\"|[^\" \t\r\n]+)[ \t]+\"?[^\" \t\r\n)]+" _files "${_text}")
if(NOT _files)
    message(FATAL_ERROR "no FILE entries found in ${_vita}: the check reads nothing")
endif()
set(_allowed "^sce_sys/" "^data/menu_logo\\.rgba$" "^data/achievements_en\\.json$" "^data/licenses/")
set(_bad "")
foreach(_f ${_files})
    string(REGEX REPLACE "^FILE[ \t]+(\"[^\"]+\"|[^\" \t\r\n]+)[ \t]+\"?([^\" \t\r\n)]+)$" "\\2" _dest "${_f}")
    set(_ok FALSE)
    foreach(_a ${_allowed})
        if(_dest MATCHES "${_a}")
            set(_ok TRUE)
        endif()
    endforeach()
    if(NOT _ok)
        list(APPEND _bad "${_dest}")
    endif()
endforeach()
if(_bad)
    string(REPLACE ";" "\n  " _list "${_bad}")
    message(FATAL_ERROR "the vpk would carry game data:\n  ${_list}")
endif()
message(STATUS "vpk: LiveArea art and the port's own files only")
