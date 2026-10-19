// GL compatibility seam. The #ifdef __vita__ split here is the only platform
// branch allowed in graphics code.
//
// On Vita, vitaGL provides every entry point directly.
// On Windows, opengl32 exports only GL 1.1, so GL 2.x entry points are resolved
// through SDL_GL_GetProcAddress. The table below lists exactly the subset this
// project may use (GL 2.1 / GLSL 120, a GLES2-equivalent feature set): anything
// outside it fails to build on desktop, so "never write desktop-only GL" is
// enforced by the build rather than by discipline.
#pragma once

// We supply our own main(); do not let SDL2main hijack it. This also avoids
// linking static SDL2main against a DLL SDL2 on Windows, which yields undefined
// SDL_strlen / SDL_memcpy at link time.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <cstdio>

#ifdef __vita__

  #include <vitaGL.h>
  inline bool GlLoadFunctions() { return true; }

#else

  #include <SDL2/SDL_opengl.h>
  #include <SDL2/SDL_opengl_glext.h>

  // glActiveTexture (GL 1.3) is declared by the MinGW GL header but not
  // exported by opengl32.dll, so it still has to be resolved dynamically. The
  // pointer gets a distinct name and an alias, or declaration and pointer
  // collide. The pointer itself is declared by the X-macro table below.

  // X(type, pointerName, glName)
  #define GL_FUNC_LIST                                                                          \
    X(PFNGLCREATESHADERPROC,              glCreateShader,            "glCreateShader")          \
    X(PFNGLSHADERSOURCEPROC,              glShaderSource,            "glShaderSource")          \
    X(PFNGLCOMPILESHADERPROC,             glCompileShader,           "glCompileShader")         \
    X(PFNGLGETSHADERIVPROC,               glGetShaderiv,             "glGetShaderiv")           \
    X(PFNGLGETSHADERINFOLOGPROC,          glGetShaderInfoLog,        "glGetShaderInfoLog")      \
    X(PFNGLDELETESHADERPROC,              glDeleteShader,            "glDeleteShader")          \
    X(PFNGLCREATEPROGRAMPROC,             glCreateProgram,           "glCreateProgram")         \
    X(PFNGLATTACHSHADERPROC,              glAttachShader,            "glAttachShader")          \
    X(PFNGLBINDATTRIBLOCATIONPROC,        glBindAttribLocation,      "glBindAttribLocation")    \
    X(PFNGLLINKPROGRAMPROC,               glLinkProgram,             "glLinkProgram")           \
    X(PFNGLGETPROGRAMIVPROC,              glGetProgramiv,            "glGetProgramiv")          \
    X(PFNGLGETPROGRAMINFOLOGPROC,         glGetProgramInfoLog,       "glGetProgramInfoLog")     \
    X(PFNGLUSEPROGRAMPROC,                glUseProgram,              "glUseProgram")            \
    X(PFNGLGETUNIFORMLOCATIONPROC,        glGetUniformLocation,      "glGetUniformLocation")    \
    X(PFNGLUNIFORM1IPROC,                 glUniform1i,               "glUniform1i")             \
    X(PFNGLUNIFORM1FPROC,                 glUniform1f,               "glUniform1f")             \
    X(PFNGLUNIFORM2FPROC,                 glUniform2f,               "glUniform2f")             \
    X(PFNGLUNIFORM3FPROC,                 glUniform3f,               "glUniform3f")             \
    X(PFNGLUNIFORM4FPROC,                 glUniform4f,               "glUniform4f")             \
    X(PFNGLUNIFORM4FVPROC,                glUniform4fv,              "glUniform4fv")            \
    X(PFNGLUNIFORMMATRIX4FVPROC,          glUniformMatrix4fv,        "glUniformMatrix4fv")      \
    X(PFNGLGENBUFFERSPROC,                glGenBuffers,              "glGenBuffers")            \
    X(PFNGLBINDBUFFERPROC,                glBindBuffer,              "glBindBuffer")            \
    X(PFNGLBUFFERDATAPROC,                glBufferData,              "glBufferData")            \
    X(PFNGLDELETEBUFFERSPROC,             glDeleteBuffers,           "glDeleteBuffers")         \
    X(PFNGLDELETEPROGRAMPROC,             glDeleteProgram,           "glDeleteProgram")         \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,   glEnableVertexAttribArray, "glEnableVertexAttribArray")\
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray,"glDisableVertexAttribArray")\
    X(PFNGLVERTEXATTRIBPOINTERPROC,       glVertexAttribPointer,     "glVertexAttribPointer")   \
    X(PFNGLACTIVETEXTUREPROC,             p_glActiveTexture,         "glActiveTexture")             X(PFNGLCOMPRESSEDTEXIMAGE2DPROC,     p_glCompressedTexImage2D,  "glCompressedTexImage2D")  \
    X(PFNGLGENFRAMEBUFFERSPROC,           glGenFramebuffers,         "glGenFramebuffers")       \
    X(PFNGLBINDFRAMEBUFFERPROC,           glBindFramebuffer,         "glBindFramebuffer")       \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,      glFramebufferTexture2D,    "glFramebufferTexture2D")  \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC,    glCheckFramebufferStatus,  "glCheckFramebufferStatus")\
    X(PFNGLGENRENDERBUFFERSPROC,          glGenRenderbuffers,        "glGenRenderbuffers")      \
    X(PFNGLBINDRENDERBUFFERPROC,          glBindRenderbuffer,        "glBindRenderbuffer")      \
    X(PFNGLRENDERBUFFERSTORAGEPROC,       glRenderbufferStorage,     "glRenderbufferStorage")   \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC,   glFramebufferRenderbuffer, "glFramebufferRenderbuffer")\
    X(PFNGLDELETEFRAMEBUFFERSPROC,        glDeleteFramebuffers,      "glDeleteFramebuffers")    \
    X(PFNGLDELETERENDERBUFFERSPROC,      glDeleteRenderbuffers,     "glDeleteRenderbuffers")

  #define X(type, ptr, glname) inline type ptr = nullptr;
  GL_FUNC_LIST
  #undef X

  #define glActiveTexture p_glActiveTexture
  // Core GLES2 and GL 1.3; declared by the MinGW header, not exported by
  // opengl32.dll, so the same aliasing as glActiveTexture. The surface pages
  // are its one caller (DXT1, and DXT5 where a page has alpha).
  #define glCompressedTexImage2D p_glCompressedTexImage2D

  inline bool GlLoadFunctions() {
      bool ok = true;
  #define X(type, ptr, glname)                                        \
      ptr = (type)SDL_GL_GetProcAddress(glname);                      \
      if (!ptr) { std::printf("GL load FAILED: %s\n", glname); ok = false; }
      GL_FUNC_LIST
  #undef X
      return ok;
  }

#endif

// Opens the window and creates the GL context.
//
// The probe hardcoded 960x544 and forced vsync OFF because it was measuring
// true frame cost. The game presents rather than measures, so size and vsync
// are parameters and vsync defaults ON.
inline bool GlCompatInit(SDL_Window** outWin, SDL_GLContext* outCtx,
                         const char* title, int width, int height,
                         bool vsync) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
#ifdef __vita__
    // vitaGL owns the GL context on Vita; SDL provides input, audio and the
    // window only.
    //
    // The parameters matter. ram_threshold is how much RAM vitaGL leaves
    // alone, so a large value starves its own internal pools; every working
    // vitaGL sample passes 4-8 MB. A starved pool crashes with r0=0xfffffff8
    // (a heap allocator reading a block header after an allocation returned
    // NULL), which 16 MB did.
    vglInitExtended(0x100000,          // 1 MB legacy pool (client arrays only)
                    960, 544,          // native panel resolution
                    0x800000,          // 8 MB RAM threshold, as per samples
                    SCE_GXM_MULTISAMPLE_NONE);

    // vitaGL triple-buffers by default. Rendering unbounded then queues frames
    // faster than the display retires them, which can exhaust display-queue
    // resources. Vsync on is the safe default; a late probe stage turns it off
    // deliberately, once rendering is known to be stable.
    // The Vita panel is fixed at 960x544, so width/height are advisory here;
    // the render target size is chosen by the renderer, not the window.
    (void)title; (void)width; (void)height;
    vglWaitVblankStart(vsync ? GL_TRUE : GL_FALSE);

    *outWin = nullptr;
    *outCtx = nullptr;
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* w = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height, SDL_WINDOW_OPENGL);
    if (!w) { std::printf("CreateWindow failed: %s\n", SDL_GetError()); return false; }
    SDL_GLContext c = SDL_GL_CreateContext(w);
    if (!c) { std::printf("CreateContext failed: %s\n", SDL_GetError()); return false; }
    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
    if (!GlLoadFunctions()) {
        std::printf("FAIL: required GL 2.1 subset unavailable\n");
        return false;
    }
    *outWin = w;
    *outCtx = c;
#endif
    return true;
}

inline GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, nullptr, log);
        std::printf("shader compile FAILED (%s): %s\n",
                    type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        return 0;
    }
    return s;
}

inline GLuint CompileProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_uv");
    glBindAttribLocation(p, 2, "a_color");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p, sizeof(log) - 1, nullptr, log);
        std::printf("program link FAILED: %s\n", log);
        return 0;
    }
    return p;
}

// Vsync control. On desktop this maps to the SDL swap interval; on Vita it maps
// to vglWaitVblankStart. Used by the probe to measure true frame cost only
// after rendering has been shown to be stable.
inline void GlCompatSetVsync(bool on) {
#ifdef __vita__
    vglWaitVblankStart(on ? GL_TRUE : GL_FALSE);
#else
    SDL_GL_SetSwapInterval(on ? 1 : 0);
#endif
}

// Rows are tightly packed. Desktop GL defaults to 4-byte row alignment, which
// corrupts any texture whose row is not a multiple of 4 bytes (the 265 px
// brackets, the LA8 font atlas). vitaGL always reads tight rows and rejects the
// call with a sticky GL_INVALID_ENUM (vitaGL textures.c:1704), which the next
// glGetError would blame on a good DXT upload, so the call is desktop-only.
inline void GlCompatTightUnpack() {
#ifndef __vita__
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#endif
}

inline void GlCompatSwap(SDL_Window* w) {
#ifdef __vita__
    (void)w;
    vglSwapBuffers(GL_FALSE);
#else
    SDL_GL_SwapWindow(w);
#endif
}

inline void GlCompatQuit(SDL_Window* w, SDL_GLContext c) {
#ifdef __vita__
    // This vitaGL exposes no explicit teardown entry point; resources are
    // reclaimed when the process exits.
    (void)w; (void)c;
#else
    if (c) SDL_GL_DeleteContext(c);
    if (w) SDL_DestroyWindow(w);
#endif
    SDL_Quit();
}
