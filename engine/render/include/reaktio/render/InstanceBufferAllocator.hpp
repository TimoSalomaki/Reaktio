#pragma once

#include "reaktio/render/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace reaktio::render {

struct QuadInstanceData {
    float position_x{};
    float position_y{};
    float size_x{1.0f};
    float size_y{1.0f};
    float rotation_radians{};
    std::uint32_t abgr{0xffffffffu};
    float depth{};
    float reserved{};
};

struct InstanceBufferBudget {
    std::uint32_t max_instances{16384u};
    std::uint32_t allocated_instances{};
    std::uint32_t allocation_count{};
    std::uint32_t failed_allocations{};
};

struct InstanceBufferSummary {
    std::uint32_t total_allocated_instances{};
    std::uint32_t total_allocations{};
    std::uint32_t total_failed{};
};

class InstanceBufferAllocator {
  public:
    InstanceBufferAllocator();
    ~InstanceBufferAllocator();

    InstanceBufferAllocator(const InstanceBufferAllocator&) = delete;
    InstanceBufferAllocator& operator=(const InstanceBufferAllocator&) = delete;
    InstanceBufferAllocator(InstanceBufferAllocator&&) noexcept;
    InstanceBufferAllocator& operator=(InstanceBufferAllocator&&) noexcept;

    bool initialize() noexcept;
    void shutdown() noexcept;
    void begin_frame() noexcept;

    void set_instance_budget(std::uint32_t max_instances) noexcept;

    [[nodiscard]] bool supports_instancing() const noexcept;
    [[nodiscard]] bool can_allocate(std::uint32_t instance_count) const noexcept;
    [[nodiscard]] bool submit_quads(
        RenderView view,
        std::span<const QuadInstanceData> instances,
        bool write_depth = false) noexcept;

    [[nodiscard]] std::uint32_t remaining_instances() const noexcept;
    [[nodiscard]] const InstanceBufferBudget& budget() const noexcept;
    [[nodiscard]] InstanceBufferSummary summary() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    InstanceBufferBudget budget_;
    InstanceBufferSummary summary_;
};

} // namespace reaktio::render