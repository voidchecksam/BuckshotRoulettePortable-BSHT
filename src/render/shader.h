// A GLSL 120 shader program.
//
// GLSL 120 / GLES 1.00 only: `attribute`, `varying`, `gl_FragColor`. No `in`,
// no `out`, no `layout(location=)`. The graphics_within_gles2_subset test
// rejects any embedded #version other than 120, so a desktop-only shader cannot
// reach hardware unnoticed.
//
// Attribute locations are BOUND BY NAME before linking rather than queried,
// because GLES2 has no layout qualifiers and vitaGL's linker does not promise
// the same automatic indices as a desktop driver.
#pragma once

#include "platform/gl_compat.h"

#include "core/mat4.h"

#include <string>
#include <utility>
#include <vector>

namespace render {

// Fixed attribute slots, bound for every program so one vertex layout works
// across all of them.
enum AttribSlot : GLuint {
    kAttribPosition = 0,   // a_pos
    kAttribNormal   = 1,   // a_nrm
    kAttribUv       = 2,   // a_uv
    kAttribColour   = 3,   // a_col
    kAttribUv2      = 4,   // a_uv2: the surface page's UV set
    kAttribDyn      = 5,   // a_dyn: the animated lights' baked layers
    kAttribJoints   = 6,   // a_joints: GPU skinning's palette slots
    kAttribWeights  = 7,   // a_weights: and their weights
};

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Logs the full driver info log on failure. A shader that fails silently
    // renders black, which looks exactly like a geometry or transform bug and
    // costs hours to chase.
    bool Compile(const char* vertexSrc, const char* fragmentSrc, const char* debugName);

    void Use() const;
    void Destroy();

    bool Valid() const { return program_ != 0; }
    GLuint Program() const { return program_; }

    // Cached per program: glGetUniformLocation is a by-name search (on vitaGL
    // two sceGxmProgramFindParameterByName calls plus a scan), and the
    // renderer sets several uniforms per draw: over a thousand searches a
    // frame on a busy table. Keyed by the name pointer first (the callers pass
    // literals), then by content, so a different pointer to the same name
    // still hits.
    //
    // A vitaGL location without STRICT_UNIFORMS_COMPLIANCE is the negated
    // address of its uniform record. User memory on the Vita sits above
    // 0x80000000, so the negation is positive and `>= 0` holds by the memory
    // map, not by contract. -1 still means "absent".
    GLint Uniform(const char* name) const;

    // The driver's log from the last failed compile or link, any program. On
    // the Vita printf goes nowhere, so this is how a rejected shader reaches
    // the report instead of reading as a black screen.
    static const std::string& LastError();

    void SetMat4(const char* name, const core::Mat4& m) const;
    void SetFloat(const char* name, float v) const;
    void SetVec2(const char* name, float x, float y) const;
    void SetVec3(const char* name, float x, float y, float z) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;
    void SetInt(const char* name, int v) const;

private:
    GLuint program_ = 0;
    mutable std::vector<std::pair<const char*, GLint>> locations_;
};

}  // namespace render
