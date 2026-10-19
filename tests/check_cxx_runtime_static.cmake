# The desktop binaries carry their own C++ runtime (MinGW only).
#
# A MinGW exe that imports libstdc++-6.dll takes whichever copy comes first on
# PATH. Git for Windows puts an older one in /mingw64/bin, ahead of MSYS2's in
# the default shell, and it lacks symbols g++ 16 code imports (std::filesystem
# pulls __codecvt_utf8_utf16_base<wchar_t>::do_unshift). Every launch of such
# an exe then stops on a modal "Entry Point Not Found" dialog, one per ctest
# process.
#
#   cmake -DOBJDUMP=<objdump> -DBINARIES="a.exe;b.exe" -P check_cxx_runtime_static.cmake
if(NOT OBJDUMP OR NOT BINARIES)
    message(FATAL_ERROR "usage: -DOBJDUMP=<objdump> -DBINARIES=<list> -P check_cxx_runtime_static.cmake")
endif()
set(_bad "")
foreach(_bin ${BINARIES})
    if(NOT EXISTS "${_bin}")
        message(FATAL_ERROR "${_bin} does not exist: build it first")
    endif()
    execute_process(COMMAND "${OBJDUMP}" -p "${_bin}" OUTPUT_VARIABLE _dump RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "objdump failed on ${_bin}")
    endif()
    foreach(_dll "libstdc++-6.dll" "libgcc_s_seh-1.dll" "libwinpthread-1.dll")
        string(FIND "${_dump}" "DLL Name: ${_dll}" _at)
        if(NOT _at EQUAL -1)
            list(APPEND _bad "${_bin} imports ${_dll}")
        endif()
    endforeach()
endforeach()
if(_bad)
    string(REPLACE ";" "\n  " _list "${_bad}")
    message(FATAL_ERROR "a desktop binary depends on the C++ runtime found on PATH:\n  ${_list}")
endif()
message(STATUS "every checked binary carries its own C++ runtime")
