#include "render/shader.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace render {

namespace {

std::string g_lastError;

GLuint CompileStage(GLenum type, const char* src, const char* debugName) {
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetShaderInfoLog(s, static_cast<GLsizei>(sizeof(log) - 1), nullptr, log);
        std::printf("shader compile FAILED [%s %s]: %s\n",
                    debugName ? debugName : "?",
                    type == GL_VERTEX_SHADER ? "vert" : "frag", log);
        g_lastError = std::string(debugName ? debugName : "?") +
                      (type == GL_VERTEX_SHADER ? " vert: " : " frag: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

const std::string& Shader::LastError() { return g_lastError; }

Shader::~Shader() { Destroy(); }

void Shader::Destroy() {
    locations_.clear();
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

bool Shader::Compile(const char* vertexSrc, const char* fragmentSrc,
                     const char* debugName) {
    Destroy();
    if (!vertexSrc || !fragmentSrc) return false;

    const GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSrc, debugName);
    if (!vs) return false;
    const GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc, debugName);
    if (!fs) { glDeleteShader(vs); return false; }

    const GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);

    // Bound before linking: GLES2 has no layout qualifiers, and relying on the
    // driver to pick matching indices works on desktop and is not promised by
    // vitaGL.
    glBindAttribLocation(p, kAttribPosition, "a_pos");
    glBindAttribLocation(p, kAttribNormal,   "a_nrm");
    glBindAttribLocation(p, kAttribUv,       "a_uv");
    glBindAttribLocation(p, kAttribColour,   "a_col");
    glBindAttribLocation(p, kAttribUv2,      "a_uv2");
    glBindAttribLocation(p, kAttribDyn,      "a_dyn");
    glBindAttribLocation(p, kAttribJoints,   "a_joints");
    glBindAttribLocation(p, kAttribWeights,  "a_weights");

    glLinkProgram(p);

    // The stages are attached to the program, which holds its own reference, so
    // they can be released whether or not the link succeeded.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        glGetProgramInfoLog(p, static_cast<GLsizei>(sizeof(log) - 1), nullptr, log);
        std::printf("program link FAILED [%s]: %s\n", debugName ? debugName : "?", log);
        g_lastError = std::string(debugName ? debugName : "?") + " link: " + log;
        glDeleteProgram(p);
        return false;
    }

    program_ = p;
    return true;
}

void Shader::Use() const {
    if (program_) glUseProgram(program_);
}

GLint Shader::Uniform(const char* name) const {
    if (!program_ || !name) return -1;
    for (const auto& [key, loc] : locations_) {
        if (key == name) return loc;
    }
    for (const auto& [key, loc] : locations_) {
        if (std::strcmp(key, name) == 0) return loc;
    }
    const GLint loc = glGetUniformLocation(program_, name);
    // The key must outlive the cache: callers pass literals, which do.
    locations_.emplace_back(name, loc);
    return loc;
}

void Shader::SetMat4(const char* name, const core::Mat4& m) const {
    const GLint loc = Uniform(name);
    // GL_FALSE: the matrices are already column-major, and GLES2 does not
    // support transpose here at all. GL_TRUE is a silent no-op on some drivers
    // and an error on others.
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m.m);
}

void Shader::SetFloat(const char* name, float v) const {
    const GLint loc = Uniform(name);
    if (loc >= 0) glUniform1f(loc, v);
}

void Shader::SetVec2(const char* name, float x, float y) const {
    const GLint loc = Uniform(name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

void Shader::SetVec3(const char* name, float x, float y, float z) const {
    const GLint loc = Uniform(name);
    if (loc >= 0) glUniform3f(loc, x, y, z);
}

void Shader::SetVec4(const char* name, float x, float y, float z, float w) const {
    const GLint loc = Uniform(name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

void Shader::SetInt(const char* name, int v) const {
    const GLint loc = Uniform(name);
    if (loc >= 0) glUniform1i(loc, v);
}

}  // namespace render
