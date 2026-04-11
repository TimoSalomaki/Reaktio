#include "reaktio/render/InstanceBufferAllocator.hpp"

#include <bgfx/bgfx.h>

#include <cstring>

namespace reaktio::render {

namespace {

struct PosColorVertex {
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

constexpr PosColorVertex k_unit_quad_vertices[6] = {
    {-0.5f, -0.5f, 0.0f, 0xffffffffu},
    {0.5f, -0.5f, 0.0f, 0xffffffffu},
    {0.5f, 0.5f, 0.0f, 0xffffffffu},
    {-0.5f, -0.5f, 0.0f, 0xffffffffu},
    {0.5f, 0.5f, 0.0f, 0xffffffffu},
    {-0.5f, 0.5f, 0.0f, 0xffffffffu},
};

bgfx::VertexLayout make_pos_color_layout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return layout;
}

std::uint64_t make_state_flags(bool write_depth) noexcept {
    std::uint64_t flags = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA;
    if (write_depth) {
        flags |= BGFX_STATE_WRITE_Z;
    }
    return flags;
}

} // namespace

struct InstanceBufferAllocator::Impl {
    bgfx::VertexLayout quad_layout = make_pos_color_layout();
    bgfx::VertexBufferHandle quad_vertex_buffer = BGFX_INVALID_HANDLE;
    bool initialized{};
    bool supports_instancing{};
};

InstanceBufferAllocator::InstanceBufferAllocator()
    : impl_(std::make_unique<Impl>()) {}

InstanceBufferAllocator::~InstanceBufferAllocator() {
    shutdown();
}

InstanceBufferAllocator::InstanceBufferAllocator(InstanceBufferAllocator&&) noexcept = default;

InstanceBufferAllocator& InstanceBufferAllocator::operator=(InstanceBufferAllocator&&) noexcept = default;

bool InstanceBufferAllocator::initialize() noexcept {
    if (impl_->initialized) {
        return true;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    impl_->supports_instancing = caps != nullptr && (caps->supported & BGFX_CAPS_INSTANCING) != 0u;
    const bgfx::Memory* memory = bgfx::copy(k_unit_quad_vertices, sizeof(k_unit_quad_vertices));
    impl_->quad_vertex_buffer = bgfx::createVertexBuffer(memory, impl_->quad_layout);
    if (!bgfx::isValid(impl_->quad_vertex_buffer)) {
        impl_->supports_instancing = false;
        return false;
    }

    impl_->initialized = true;
    return true;
}

void InstanceBufferAllocator::shutdown() noexcept {
    if (impl_ != nullptr && bgfx::isValid(impl_->quad_vertex_buffer)) {
        bgfx::destroy(impl_->quad_vertex_buffer);
        impl_->quad_vertex_buffer = BGFX_INVALID_HANDLE;
    }

    if (impl_ != nullptr) {
        impl_->initialized = false;
        impl_->supports_instancing = false;
    }
}

void InstanceBufferAllocator::begin_frame() noexcept {
    budget_.allocated_instances = 0;
    budget_.allocation_count = 0;
    budget_.failed_allocations = 0;
}

void InstanceBufferAllocator::set_instance_budget(std::uint32_t max_instances) noexcept {
    budget_.max_instances = max_instances;
}

bool InstanceBufferAllocator::supports_instancing() const noexcept {
    return impl_ != nullptr && impl_->initialized && impl_->supports_instancing;
}

bool InstanceBufferAllocator::can_allocate(std::uint32_t instance_count) const noexcept {
    return instance_count > 0 && instance_count <= remaining_instances();
}

bool InstanceBufferAllocator::submit_quads(
    RenderView view,
    std::span<const QuadInstanceData> instances,
    bool write_depth) noexcept {
    if (instances.empty()) {
        return true;
    }

    if (!supports_instancing()) {
        return false;
    }

    const std::uint32_t instance_count = static_cast<std::uint32_t>(instances.size());
    if (!can_allocate(instance_count) ||
        !bgfx::getAvailInstanceDataBuffer(instance_count, static_cast<std::uint16_t>(sizeof(QuadInstanceData)))) {
        ++budget_.failed_allocations;
        ++summary_.total_failed;
        return false;
    }

    bgfx::InstanceDataBuffer instance_buffer{};
    bgfx::allocInstanceDataBuffer(&instance_buffer, instance_count, static_cast<std::uint16_t>(sizeof(QuadInstanceData)));
    std::memcpy(instance_buffer.data, instances.data(), instances.size_bytes());

    bgfx::setState(make_state_flags(write_depth));
    bgfx::setVertexBuffer(0, impl_->quad_vertex_buffer);
    bgfx::setInstanceDataBuffer(&instance_buffer);
    bgfx::submit(to_view_id(view), BGFX_INVALID_HANDLE);

    budget_.allocated_instances += instance_count;
    ++budget_.allocation_count;
    summary_.total_allocated_instances += instance_count;
    ++summary_.total_allocations;
    return true;
}

std::uint32_t InstanceBufferAllocator::remaining_instances() const noexcept {
    if (budget_.allocated_instances >= budget_.max_instances) {
        return 0;
    }

    return budget_.max_instances - budget_.allocated_instances;
}

const InstanceBufferBudget& InstanceBufferAllocator::budget() const noexcept {
    return budget_;
}

InstanceBufferSummary InstanceBufferAllocator::summary() const noexcept {
    return summary_;
}

} // namespace reaktio::render