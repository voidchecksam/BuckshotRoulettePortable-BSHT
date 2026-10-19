# Every exporter script bsht-extract asks for exists in the folder the release
# copies them from, so the packaged exporters/ folder is complete.
#
#   cmake -DTOOL=<bsht-extract> -DDIR=<tools/godot> -P check_exporter_list.cmake
execute_process(COMMAND "${TOOL}" --list-exporters OUTPUT_VARIABLE _out RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "${TOOL} --list-exporters exited ${_rc}")
endif()
string(REPLACE "\r" "" _out "${_out}")
string(REPLACE "\n" ";" _names "${_out}")
set(_count 0)
foreach(_name IN LISTS _names)
    if(_name STREQUAL "")
        continue()
    endif()
    if(NOT EXISTS "${DIR}/${_name}")
        message(FATAL_ERROR "bsht-extract needs ${_name}, which is not in ${DIR}")
    endif()
    math(EXPR _count "${_count} + 1")
endforeach()
if(_count EQUAL 0)
    message(FATAL_ERROR "${TOOL} --list-exporters printed nothing")
endif()
message(STATUS "${_count} exporter scripts, all in ${DIR}")
