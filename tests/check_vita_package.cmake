# Every game file the Vita build reads is one bsht-extract ships.
#
# The runtime names its data files as string literals ("runtime_main.json",
# "/ui_fonts.json", Join(dataRoot, "menu.room")). On the Vita every one of them
# lives in the game folder, so each must be named in src/extract/package.cpp.
# The port's own files (the vpk's) and the save folder's files are listed below.
# Music is shipped by folder (every .ogg of audio/music plus music_list.txt),
# dialogue by locale.
#
#   cmake -DROOT=<source dir> [-DDROP=<file name>] -P check_vita_package.cmake
#
# DROP removes that name from package.cpp's text first: the negative test.
if(NOT ROOT)
    message(FATAL_ERROR "usage: cmake -DROOT=<source dir> -P check_vita_package.cmake")
endif()
file(READ "${ROOT}/src/extract/package.cpp" _package_text)
if(DEFINED DROP)
    string(REPLACE "\"${DROP}\"" "\"\"" _package_text "${_package_text}")
endif()

file(GLOB _readers "${ROOT}/src/app/*.cpp")
list(APPEND _readers "${ROOT}/src/main_vita.cpp" "${ROOT}/src/godot/translation.cpp")
set(_not_game "menu_logo.rgba" "achievements_en.json" "scenario.json" "manifest.json")
# Shipped by the audio/music folder copy under its flat name (Flat(m)), checked
# through OggsIn below: the menu's track, "music main_gate modded.ogg".
list(APPEND _not_game "music_main_gate_modded.ogg")

set(_names "")
foreach(_file ${_readers})
    file(READ "${_file}" _text)
    string(REGEX MATCHALL "\"/?[A-Za-z0-9_]+\\.(json|bin|room|bank|rgba|ogg)\"" _found "${_text}")
    foreach(_f ${_found})
        string(REGEX REPLACE "^\"/?(.*)\"$" "\\1" _name "${_f}")
        list(FIND _not_game "${_name}" _skip)
        if(_skip EQUAL -1)
            list(APPEND _names "${_name}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES _names)
list(LENGTH _names _count)
if(_count LESS 20)
    message(FATAL_ERROR "only ${_count} data file names found in src/app: the check reads too little")
endif()

set(_missing "")
foreach(_name ${_names})
    string(FIND "${_package_text}" "\"${_name}\"" _at)
    if(_at EQUAL -1)
        list(APPEND _missing "${_name}")
    endif()
endforeach()

foreach(_need "music_list.txt" "OggsIn(ex / \"audio\" / \"music\")" "\"extracted/dialogue_\"")
    string(FIND "${_package_text}" "${_need}" _at)
    if(_at EQUAL -1)
        list(APPEND _missing "${_need}")
    endif()
endforeach()

if(_missing)
    string(REPLACE ";" "\n  " _list "${_missing}")
    message(FATAL_ERROR "src/extract/package.cpp does not ship what the device loads:\n  ${_list}")
endif()
message(STATUS "game folder: all ${_count} data files the runtime names are shipped")
