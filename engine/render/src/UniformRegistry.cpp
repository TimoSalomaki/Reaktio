#include "reaktio/render/UniformRegistry.hpp"

namespace reaktio::render {

UniformHandle UniformRegistry::register_uniform(std::string_view name, UniformType type) {
    if (name.empty()) {
        return {};
    }

    if (auto it = lookup_.find(std::string(name)); it != lookup_.end()) {
        return it->second;
    }

    const std::uint32_t index = static_cast<std::uint32_t>(records_.size()) + 1u;
    const UniformHandle handle{index};

    records_.push_back(UniformRecord{
        .handle = handle,
        .name = std::string(name),
        .type = type,
    });

    lookup_.emplace(std::string(name), handle);
    return handle;
}

UniformHandle UniformRegistry::resolve(std::string_view name) const noexcept {
    if (name.empty()) {
        return {};
    }

    const auto it = lookup_.find(std::string(name));
    return (it != lookup_.end()) ? it->second : UniformHandle{};
}

const UniformRecord* UniformRegistry::try_get(UniformHandle handle) const noexcept {
    if (!handle.valid()) {
        return nullptr;
    }

    const std::uint32_t index = handle.value() - 1u;
    if (index >= records_.size()) {
        return nullptr;
    }

    return &records_[index];
}

std::size_t UniformRegistry::uniform_count() const noexcept {
    return records_.size();
}

void UniformRegistry::clear() noexcept {
    records_.clear();
    lookup_.clear();
}

} // namespace reaktio::render
