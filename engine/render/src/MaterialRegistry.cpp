#include "reaktio/render/MaterialRegistry.hpp"

#include <algorithm>

namespace reaktio::render {

MaterialHandle MaterialRegistry::register_material(
    std::string_view name,
    ShaderProgramHandle program) {
    if (name.empty()) {
        return {};
    }

    if (auto it = lookup_.find(std::string(name)); it != lookup_.end()) {
        return it->second;
    }

    MaterialRecord record{
        .name = std::string(name),
        .program = program,
    };

    const MaterialHandle handle = records_.emplace(std::move(record));
    if (!handle.valid()) {
        return {};
    }

    if (MaterialRecord* stored = records_.try_get(handle)) {
        stored->handle = handle;
    }

    lookup_.emplace(std::string(name), handle);
    return handle;
}

bool MaterialRegistry::set_uniform(MaterialHandle handle, UniformHandle uniform, UniformValue value) {
    MaterialRecord* record = records_.try_get(handle);
    if (record == nullptr || !uniform.valid()) {
        return false;
    }

    for (UniformBinding& binding : record->bindings) {
        if (binding.uniform == uniform) {
            binding.value = std::move(value);
            return true;
        }
    }

    record->bindings.push_back(UniformBinding{
        .uniform = uniform,
        .value = std::move(value),
    });
    return true;
}

bool MaterialRegistry::release_material(MaterialHandle handle) noexcept {
    const MaterialRecord* record = records_.try_get(handle);
    if (record == nullptr) {
        return false;
    }

    lookup_.erase(record->name);
    return records_.erase(handle);
}

void MaterialRegistry::clear() noexcept {
    lookup_.clear();
    records_.clear();
}

bool MaterialRegistry::contains(MaterialHandle handle) const noexcept {
    return records_.contains(handle);
}

const MaterialRecord* MaterialRegistry::try_get(MaterialHandle handle) const noexcept {
    return records_.try_get(handle);
}

MaterialHandle MaterialRegistry::resolve(std::string_view name) const noexcept {
    if (name.empty()) {
        return {};
    }

    const auto it = lookup_.find(std::string(name));
    if (it == lookup_.end()) {
        return {};
    }

    return records_.contains(it->second) ? it->second : MaterialHandle{};
}

std::size_t MaterialRegistry::material_count() const noexcept {
    return records_.size();
}

MaterialRegistrySummary MaterialRegistry::summary() const noexcept {
    return MaterialRegistrySummary{
        .material_count = records_.size(),
    };
}

} // namespace reaktio::render
