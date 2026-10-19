# Fails if graphics code uses GL outside the GLES2-equivalent subset.
#
# gl_compat.h already guards the modern entry points: they are resolved through
# an explicit X-macro table, so anything not listed does not exist and fails to
# build. That table does not guard the other direction.
#
# On Windows, opengl32 exports the whole fixed-function pipeline and
# SDL_opengl.h declares it. glBegin, glMatrixMode and glVertexPointer therefore
# compile and run on desktop, and are absent or unreliable on GLES2. Without
# this check the failure surfaces only on the device. Catch it at the desk.
#
# Invoked as: cmake -DSRC=<repo>/src -P check_gl_subset.cmake

file(GLOB_RECURSE SRCS
     "${SRC}/platform/*.h" "${SRC}/platform/*.cpp"
     "${SRC}/render/*.h"   "${SRC}/render/*.cpp"
     "${SRC}/game/*.h"     "${SRC}/game/*.cpp"
     "${SRC}/*.cpp")

# Fixed-function and desktop-only entry points. Each is a real GLES2 absence,
# not a style preference.
set(BANNED_CALLS
    # immediate mode
    "glBegin" "glEnd(" "glVertex2" "glVertex3" "glVertex4"
    "glColor3" "glColor4" "glNormal3" "glTexCoord2"
    # fixed-function matrix stack: the port carries its own matrices
    "glMatrixMode" "glLoadIdentity" "glLoadMatrix" "glMultMatrix"
    "glPushMatrix" "glPopMatrix" "glTranslate" "glRotate" "glScale"
    "glOrtho" "glFrustum"
    # fixed-function lighting/material/texenv: the port bakes its light
    "glLightf" "glLightModel" "glMaterialf" "glTexEnv" "glShadeModel"
    "glAlphaFunc" "glFogf"
    # client arrays: GLES2 has vertex attributes only
    "glVertexPointer" "glColorPointer" "glTexCoordPointer" "glNormalPointer"
    "glEnableClientState" "glDisableClientState" "glClientActiveTexture"
    # GL3+/desktop-only
    "glGenVertexArrays" "glBindVertexArray" "glDeleteVertexArrays"
    "glDrawArraysInstanced" "glDrawElementsInstanced" "glVertexAttribDivisor"
    "glDrawBuffers" "glMapBuffer" "glTexImage3D" "glGetStringi"
    "glBlitFramebuffer" "glRenderbufferStorageMultisample")

# vitaGL, not GLES2: its glPixelStorei accepts only GL_UNPACK_ROW_LENGTH and
# raises GL_INVALID_ENUM for anything else (vitaGL source/textures.c:1704).
# vitaGL keeps one sticky error, so the next glGetError, after a good DXT
# upload, reports it, every baked page is deleted and the whole room draws
# white on the device. Desktop GL accepts the call, so only this check catches
# it before the device does. main_desktop.cpp is exempt: its screenshot
# readback never runs on the Vita; so is platform/gl_compat.h, whose
# GlCompatTightUnpack compiles the call out on Vita.
set(VITAGL_UNSUPPORTED "GL_UNPACK_ALIGNMENT" "GL_PACK_ALIGNMENT")

set(VIOLATIONS "")
foreach(f ${SRCS})
    file(READ "${f}" content)
    if(NOT f MATCHES "main_desktop\\.cpp$" AND NOT f MATCHES "gl_compat\\.h$")
        foreach(b ${VITAGL_UNSUPPORTED})
            string(FIND "${content}" "${b}" vhit)
            if(NOT vhit EQUAL -1)
                list(APPEND VIOLATIONS "${f} uses '${b}', which vitaGL's glPixelStorei rejects (sticky GL_INVALID_ENUM)")
            endif()
        endforeach()
    endif()
    foreach(b ${BANNED_CALLS})
        string(FIND "${content}" "${b}" hit)
        if(NOT hit EQUAL -1)
            list(APPEND VIOLATIONS "${f} uses '${b}', which is not in GLES2")
        endif()
    endforeach()

    # Shader sources are embedded as C string literals, so they are checked here
    # too. GLSL 120 uses attribute/varying/gl_FragColor, never in/out/layout.
    #
    # Match only a real directive: "#version" followed by digits. Matching the
    # bare word flags prose about the rule in the headers that document it, a
    # guard that cries wolf and gets switched off.
    string(REGEX MATCHALL "#version[ 	]+[0-9]+" VERSIONS "${content}")
    if(VERSIONS)
        foreach(v ${VERSIONS})
            if(NOT v MATCHES "#version[ 	]+120")
                list(APPEND VIOLATIONS "${f} declares '${v}'; GLSL 120 only")
            endif()
        endforeach()
        foreach(g "layout(" "texelFetch" "gl_FragData\\[1\\]")
            string(FIND "${content}" "${g}" ghit)
            if(NOT ghit EQUAL -1)
                list(APPEND VIOLATIONS "${f} shader uses '${g}', not in GLSL 120/ES 1.00")
            endif()
        endforeach()
    endif()
endforeach()

list(LENGTH SRCS n_files)
if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(SEND_ERROR "${v}")
    endforeach()
    message(FATAL_ERROR
        "graphics code must stay within the GLES2-equivalent subset so that "
        "what builds on desktop also runs on the Vita")
endif()

message(STATUS "checked ${n_files} files: graphics code is within the GLES2 subset")
