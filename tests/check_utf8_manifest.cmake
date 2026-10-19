# Every Windows exe a player runs carries a manifest that makes UTF-8 the
# process code page, so fopen and friends open files under a folder such as
# "C:\Users\Zoe K" with an accent in it.
#
#   cmake -DBINARIES=<exe;exe> -P check_utf8_manifest.cmake
foreach(_bin IN LISTS BINARIES)
    file(STRINGS "${_bin}" _hits REGEX "<activeCodePage[^>]*>UTF-8</activeCodePage>")
    if(NOT _hits)
        message(FATAL_ERROR "${_bin} has no UTF-8 activeCodePage manifest (src/platform/windows/utf8.rc)")
    endif()
    message(STATUS "utf-8 manifest: ${_bin}")
endforeach()
