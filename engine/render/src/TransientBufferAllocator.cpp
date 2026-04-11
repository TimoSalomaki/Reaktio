#include "reaktio/render/TransientBufferAllocator.hpp"

#include <bgfx/bgfx.h>

#include <cstring>

namespace reaktio::render {

namespace {

bgfx::VertexLayout make_transient_color_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return layout;
}

std::uint64_t make_state_flags(const TransientBufferSubmission& submission) noexcept {
    std::uint64_t flags = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (submission.write_depth) {
        flags |= BGFX_STATE_WRITE_Z;
    }

    switch (submission.blend_mode) {
    case BufferBlendMode::Opaque:
        break;
    case BufferBlendMode::Alpha:
        flags |= BGFX_STATE_BLEND_ALPHA;
        break;
    case BufferBlendMode::Additive:
        flags |= BGFX_STATE_BLEND_ADD;
        break;
    }

    switch (submission.primitive) {
    case BufferPrimitive::Triangles:
        break;
    case BufferPrimitive::Lines:
        flags |= BGFX_STATE_PT_LINES;
        break;
    case BufferPrimitive::Points:
        flags |= BGFX_STATE_PT_POINTS;
        break;
    }

    return flags;
}

} // namespace

struct TransientBufferAllocator::Impl {
    bgfx::VertexLayout color_layout = make_transient_color_layout();
};

TransientBufferAllocator::TransientBufferAllocator()
    : impl_(std::make_unique<Impl>()) {}

TransientBufferAllocator::~TransientBufferAllocator() = default;

TransientBufferAllocator::TransientBufferAllocator(TransientBufferAllocator&&) noexcept = default;

TransientBufferAllocator& TransientBufferAllocator::operator=(TransientBufferAllocator&&) noexcept = default;

void TransientBufferAllocator::begin_frame() noexcept {
    budget_.allocated_vertices = 0;
    budget_.allocation_count = 0;
    budget_.failed_allocations = 0;
}

void TransientBufferAllocator::set_vertex_budget(std::uint32_t max_vertices) noexcept {
    budget_.max_transient_vertices = max_vertices;
}

bool TransientBufferAllocator::submit(const TransientBufferSubmission& submission) noexcept {
    if (submission.vertices.empty()) {
        return true;
    }

    const std::uint32_t vertex_count = static_cast<std::uint32_t>(submission.vertices.size());
    if (!can_allocate(vertex_count) ||
        !bgfx::getAvailTransientVertexBuffer(vertex_count, impl_->color_layout)) {
        ++budget_.failed_allocations;
        ++summary_.total_failed;
        return false;
    }

    bgfx::TransientVertexBuffer transient_buffer{};
    bgfx::allocTransientVertexBuffer(&transient_buffer, vertex_count, impl_->color_layout);
    std::memcpy(
        transient_buffer.data,
        submission.vertices.data(),
        submission.vertices.size_bytes());

    bgfx::setState(make_state_flags(submission));
    bgfx::setVertexBuffer(0, &transient_buffer);
    bgfx::submit(to_view_id(submission.view), BGFX_INVALID_HANDLE);

    budget_.allocated_vertices += vertex_count;
    ++budget_.allocation_count;
    summary_.total_allocated_vertices += vertex_count;
    ++summary_.total_allocations;
    return true;
}

std::uint32_t TransientBufferAllocator::remaining_vertices() const noexcept {
    if (budget_.allocated_vertices >= budget_.max_transient_vertices) {
        return 0;
    }

    return budget_.max_transient_vertices - budget_.allocated_vertices;
}

bool TransientBufferAllocator::can_allocate(std::uint32_t vertex_count) const noexcept {
    return vertex_count > 0 && vertex_count <= remaining_vertices();
}

const TransientBufferBudget& TransientBufferAllocator::budget() const noexcept {
    return budget_;
}

TransientBufferSummary TransientBufferAllocator::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::render
