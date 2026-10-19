cmake_minimum_required(VERSION 3.19)

# The Vita toolchain must be selected before project(), because project() is
# what probes the compiler. Set afterwards, it silently configures a host
# build that then fails at link with unrelated-looking errors.
set(BSHT_TARGET "desktop" CACHE STRING "desktop | vita")

if(BSHT_TARGET STREQUAL "vita")
    # The SDK lives in the repo (external/vitasdk); VITASDK may still point
    # elsewhere for a one-off build.
    if(NOT DEFINED ENV{VITASDK} AND EXISTS "${CMAKE_SOURCE_DIR}/external/vitasdk")
        set(ENV{VITASDK} "${CMAKE_SOURCE_DIR}/external/vitasdk")
    endif()
    if(NOT DEFINED ENV{VITASDK})
        message(FATAL_ERROR "VITASDK is not set and external/vitasdk is missing")
    endif()
    message(STATUS "VITASDK: $ENV{VITASDK}")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake"
        CACHE STRING "" FORCE)
    # An empty build type is -O0, which costs the Vita 13-28 ms of CPU a
    # frame. -O2 -g keeps crash dumps symbolizable with addr2line.
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
    endif()
endif()

# C as well as C++: the vendored Vorbis decoder is C. It is the only C in the
# project, and it lives in third_party/.
project(bsht C CXX)
if(WIN32)
    enable_language(RC)   # the UTF-8 manifest of the Windows exes
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# MinGW: every binary carries its own C++ runtime. An exe importing
# libstdc++-6.dll takes whichever copy is first on PATH, and Git for Windows'
# older one (/mingw64/bin, ahead of MSYS2's in the default shell) lacks symbols
# g++ 16 code imports: a modal "Entry Point Not Found" per launch. Guarded by
# the desktop_binaries_carry_their_cxx_runtime CTest.
if(MINGW)
    # winpthread too: Git ships its own copy of that DLL as well.
    add_link_options(-static-libgcc -static-libstdc++
                     -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# libstdc++'s cheap precondition checks (vector::operator[] bounds, front() of
# an empty container, ...) on every build, so an out-of-range index aborts at
# its source on desktop and in the tests, not only on the Vita. An option so
# their cost can be measured on the device; on by default, the Vita included:
# an abort with a message beats silent memory corruption on hardware that only
# reports a crash dump.
option(BSHT_GLIBCXX_ASSERTIONS "libstdc++ precondition checks (_GLIBCXX_ASSERTIONS)" ON)
if(BSHT_GLIBCXX_ASSERTIONS)
    add_compile_definitions(_GLIBCXX_ASSERTIONS)
endif()
message(STATUS "bsht: _GLIBCXX_ASSERTIONS ${BSHT_GLIBCXX_ASSERTIONS}")

# Tests are host-only: they need GoogleTest and a host runner. The simulation is
# still covered, because core/, seq/, assets/ and game/ are the same sources on
# both targets, which is what the no-platform-deps guard buys.
if(BSHT_TARGET STREQUAL "vita")
    set(BSHT_BUILD_TESTS OFF)
else()
    option(BSHT_BUILD_TESTS "Build headless unit tests" ON)
endif()

add_subdirectory(src)

if(BSHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
