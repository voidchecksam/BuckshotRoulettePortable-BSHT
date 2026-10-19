#include "render/prop_chunk_gpu.h"

#include <utility>

namespace render {

PropChunkGpu::~PropChunkGpu() { Destroy(); }

PropChunkGpu::PropChunkGpu(PropChunkGpu&& other) noexcept { *this = std::move(other); }

PropChunkGpu& PropChunkGpu::operator=(PropChunkGpu&& other) noexcept {
    if (this != &other) {
        Destroy();
        vbo_ = std::exchange(other.vbo_, 0u);
        ibo_ = std::exchange(other.ibo_, 0u);
        indices_ = std::move(other.indices_);
        ranges_ = std::move(other.ranges_);
        lists_ = std::move(other.lists_);
        other.lists_.clear();
        scratch_ = std::move(other.scratch_);
        rebuilds_ = other.rebuilds_;
    }
    return *this;
}

void PropChunkGpu::Destroy() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    for (List& l : lists_) {
        if (l.ibo) glDeleteBuffers(1, &l.ibo);
    }
    vbo_ = ibo_ = 0;
    lists_.clear();
    indices_.clear();
    ranges_.clear();
}

bool PropChunkGpu::Upload(const std::vector<Vertex>& vertices, std::vector<uint16_t> indices,
                          std::vector<IndexRange> ranges) {
    Destroy();
    if (vertices.empty() || indices.empty()) return false;
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                 GL_STATIC_DRAW);
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint16_t)),
                 indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    indices_ = std::move(indices);
    ranges_ = std::move(ranges);
    lists_.reserve(kLists);
    scratch_.reserve(indices_.size());
    return true;
}

void PropChunkGpu::DrawIndices(GLuint ibo, GLsizei count, const void* offset) const {
    if (!vbo_ || !ibo || count <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    EnableVertexLayout();
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, offset);
    DisableVertexLayout();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void PropChunkGpu::DrawJoined(int key, uint32_t mask) {
    if (mask == 0) return;
    ++clock_;
    List* hit = nullptr;
    List* oldest = nullptr;
    for (List& l : lists_) {
        if (l.key == key && l.mask == mask) {
            hit = &l;
            break;
        }
        if (!oldest || l.used < oldest->used) oldest = &l;
    }
    if (!hit) {
        if (static_cast<int>(lists_.size()) < kLists) {
            lists_.push_back(List{});   // within the reserve
            hit = &lists_.back();
            glGenBuffers(1, &hit->ibo);
        } else {
            hit = oldest;
        }
        BuildIndexList(indices_, ranges_, mask, scratch_);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, hit->ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(scratch_.size() * sizeof(uint16_t)),
                     scratch_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        hit->key = key;
        hit->mask = mask;
        hit->count = static_cast<GLsizei>(scratch_.size());
        ++rebuilds_;
    }
    hit->used = clock_;
    DrawIndices(hit->ibo, hit->count, nullptr);
}

void PropChunkGpu::DrawMember(int slot) const {
    if (slot < 0 || slot >= static_cast<int>(ranges_.size())) return;
    const IndexRange& r = ranges_[static_cast<std::size_t>(slot)];
    DrawIndices(ibo_, static_cast<GLsizei>(r.count),
                reinterpret_cast<const void*>(static_cast<std::size_t>(r.first) * sizeof(uint16_t)));
}

}  // namespace render
