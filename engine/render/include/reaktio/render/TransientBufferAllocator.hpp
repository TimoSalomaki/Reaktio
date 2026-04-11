#pragma once

#include "reaktio/render/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace reaktio::render {

enum class BufferPrimitive : std::uint8_t {
    Triangles,
    Lines,
    Points,
};

enum class BufferBlendMode : std::uint8_t {
    Opaque,
    Alpha,
    Additive,
};

struct TransientColorVertex {
    float x{};
    float y{};
    float z{};
    std::uint32_t abgr{0xffffffffu};
};

struct TransientBufferSubmission {
    RenderView view{RenderView::MainScene};
    BufferPrimitive primitive{BufferPrimitive::Triangles};
    BufferBlendMode blend_mode{BufferBlendMode::Alpha};
    bool write_depth{};
    std::span<const TransientColorVertex> vertices{};
};

struct TransientBufferAllocation {
    void* vertex_data{};
    std::uint32_t vertex_count{};
    std::uint32_t vertex_stride{};
    bool valid{false};
};

struct TransientBufferBudget {
    std::uint32_t max_transient_vertices{65536u};
    std::uint32_t allocated_vertices{};
    std::uint32_t allocation_count{};
    std::uint32_t failed_allocations{};
};

struct TransientBufferSummary {
    std::uint32_t total_allocated_vertices{};
    std::uint32_t total_allocations{};
    std::uint32_t total_failed{};
};

class TransientBufferAllocator {
  public:
        TransientBufferAllocator();
        ~TransientBufferAllocator();

        TransientBufferAllocator(const TransientBufferAllocator&) = delete;
        TransientBufferAllocator& operator=(const TransientBufferAllocator&) = delete;
        TransientBufferAllocator(TransientBufferAllocator&&) noexcept;
        TransientBufferAllocator& operator=(TransientBufferAllocator&&) noexcept;

    void begin_frame() noexcept;

    void set_vertex_budget(std::uint32_t max_vertices) noexcept;
        [[nodiscard]] bool submit(const TransientBufferSubmission& submission) noexcept;

    [[nodiscard]] std::uint32_t remaining_vertices() const noexcept;
    [[nodiscard]] bool can_allocate(std::uint32_t vertex_count) const noexcept;

    [[nodiscard]] const TransientBufferBudget& budget() const noexcept;
    [[nodiscard]] TransientBufferSummary summary() const noexcept;

  private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    TransientBufferBudget budget_;
        TransientBufferSummary summary_;
};

} // namespace reaktio::render
