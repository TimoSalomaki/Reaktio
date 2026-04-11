#include "reaktio/render/ShaderProgramRegistry.hpp"

namespace reaktio::render {

ShaderProgramHandle ShaderProgramRegistry::register_program(std::string_view name) {
    if (name.empty()) {
        return {};
    }

    if (auto it = lookup_.find(std::string(name)); it != lookup_.end()) {
        return it->second;
    }

    ShaderProgramRecord record{
        .name = std::string(name),
        .loaded = false,
    };

    const ShaderProgramHandle handle = records_.emplace(std::move(record));
    if (!handle.valid()) {
        return {};
    }

    if (ShaderProgramRecord* stored = records_.try_get(handle)) {
        stored->handle = handle;
    }

    lookup_.emplace(std::string(name), handle);
    return handle;
}

bool ShaderProgramRegistry::mark_loaded(ShaderProgramHandle handle) noexcept {
    ShaderProgramRecord* record = records_.try_get(handle);
    if (record == nullptr) {
        return false;
    }

    if (!record->loaded) {
        record->loaded = true;
        ++loaded_count_;
    }

    return true;
}

bool ShaderProgramRegistry::release_program(ShaderProgramHandle handle) noexcept {
    const ShaderProgramRecord* record = records_.try_get(handle);
    if (record == nullptr) {
        return false;
    }

    if (record->loaded) {
        --loaded_count_;
    }

    lookup_.erase(record->name);
    return records_.erase(handle);
}

void ShaderProgramRegistry::clear() noexcept {
    lookup_.clear();
    records_.clear();
    loaded_count_ = 0;
}

bool ShaderProgramRegistry::contains(ShaderProgramHandle handle) const noexcept {
    return records_.contains(handle);
}

const ShaderProgramRecord* ShaderProgramRegistry::try_get(ShaderProgramHandle handle) const noexcept {
    return records_.try_get(handle);
}

ShaderProgramHandle ShaderProgramRegistry::resolve(std::string_view name) const noexcept {
    if (name.empty()) {
        return {};
    }

    const auto it = lookup_.find(std::string(name));
    if (it == lookup_.end()) {
        return {};
    }

    return records_.contains(it->second) ? it->second : ShaderProgramHandle{};
}

std::size_t ShaderProgramRegistry::program_count() const noexcept {
    return records_.size();
}

ShaderProgramRegistrySummary ShaderProgramRegistry::summary() const noexcept {
    return ShaderProgramRegistrySummary{
        .program_count = records_.size(),
        .loaded_count = loaded_count_,
    };
}

} // namespace reaktio::render
