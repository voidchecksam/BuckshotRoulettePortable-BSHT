// Draws the scene's Label3D nodes: the health counter's DEALER / PLAYER, the
// shooting choice, the restroom and signature-machine screens.
//
// Layout is assets::LayoutLabel3D, in label space, redone only when a label's
// text changes. Every label's live state (text, visible in tree, transparency,
// global transform) is read from its godot::Label3D: HealthCounter writes
// `ui_playername.text`, Bootup shows the panels, `show text` fades the shooting
// choice. The visible labels are drawn from the font atlas in one call.
#pragma once

#include "assets/font.h"
#include "assets/label3d.h"
#include "core/mat4.h"
#include "godot/scene_tree.h"
#include "render/shader.h"
#include "render/sprite_batch.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace render {

class Label3DRenderer {
public:
    Label3DRenderer() = default;
    ~Label3DRenderer();

    Label3DRenderer(const Label3DRenderer&) = delete;
    Label3DRenderer& operator=(const Label3DRenderer&) = delete;

    // Needs a GL context.
    bool Init();
    void Destroy();

    // All three must outlive this renderer. Lays every label out once.
    // `pages` holds one texture per FontAtlas page (index = page); a page
    // not uploaded (an unloaded CJK group) is simply not drawn.
    void Bind(const assets::Labels3D* labels, const assets::FontAtlas* atlas,
              const std::vector<SpriteTexture>* pages);
    bool Bound() const { return labels_ && atlas_ && pages_; }

    // Lays every label out again: the atlas gained or lost a glyph group.
    void Invalidate();

    // Resolves every label to its godot::Label3D, once, after Bind, and lays it
    // out with the node's text. Returns how many labels have no node; those are
    // not drawn.
    int BindTree(const godot::SceneTree& tree);

    // Draws after the opaque scene, depth-tested, without writing depth. Reads
    // the tree first; the vertex buffer is rebuilt only when something changed.
    void Draw(const core::Mat4& viewProj);

    int DrawnLabels() const { return drawnLabels_; }
    int DrawnQuads() const { return drawnQuads_; }
    int MissingFaces() const { return missingFaces_; }

private:
    // What was last laid out and uploaded for one label.
    struct LiveState {
        bool        visible = false;
        float       transparency = 0.0f;
        // Label3D.modulate: the glyph vertex colour (label_3d.cpp:418); the
        // outline keeps outline_modulate (label_3d.cpp:604-608).
        float       modulate[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        core::Mat4  world;
        std::string text;
    };

    // Compares every label's node with its LiveState; relays out changed text
    // and marks the buffer dirty on any change.
    void Sync();
    void Layout(std::size_t i);

    // Lays out every visible label into verts_ and uploads it.
    void Rebuild();

    struct Vertex {
        float   pos[3];
        float   uv[2];
        uint8_t col[4];
    };

    Shader shader_;
    GLuint vbo_ = 0;
    GLint  uMvp_ = -1;
    GLint  uTex_ = -1;

    const assets::Labels3D*  labels_ = nullptr;
    const assets::FontAtlas* atlas_ = nullptr;
    const std::vector<SpriteTexture>* pages_ = nullptr;
    // verts_ is sorted by page; one draw per run.
    struct PageRun { int page; GLint first; GLsizei count; };
    std::vector<PageRun> runs_;

    // Per label: its layout at transparency 0, its node, and its last state.
    std::vector<std::vector<assets::LabelQuad>> layouts_;
    std::vector<const godot::Label3D*> nodes_;
    const godot::SceneTree* tree_ = nullptr;
    uint64_t boundGeneration_ = 0;   // tree_->free_generation() nodes_ match
    std::vector<LiveState> live_;

    std::vector<Vertex> verts_;
    // Rebuilt and re-uploaded only when a label is shown, hidden, faded, moved
    // or re-texted; an unchanged frame just draws.
    bool dirty_ = true;
    GLsizei uploadedVerts_ = 0;
    int drawnLabels_ = 0;
    int drawnQuads_ = 0;
    int missingFaces_ = 0;
};

}  // namespace render
