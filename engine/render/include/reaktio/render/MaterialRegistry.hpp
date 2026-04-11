#pragma once

#include "reaktio/render/ShaderProgramRegistry.hpp"
#include "reaktio/render/UniformRegistry.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::render {

struct MaterialHandleTag;
using MaterialHandle = foundation::StrongId<MaterialHandleTag, std::uint64_t>;

struct UniformBinding {
    UniformHandle uniform{};
    UniformValue value;
};

struct MaterialRecord {
    MaterialHandle handle{};
    std::string name;
    ShaderProgramHandle program{};
    std::vector<UniformBinding> bindings;
};

struct MaterialRegistrySummary {
    std::size_t material_count{};
};

class MaterialRegistry {
  public:
    [[nodiscard]] MaterialHandle register_material(
        std::string_view name,
        ShaderProgramHandle program);
    bool set_uniform(MaterialHandle handle, UniformHandle uniform, UniformValue value);
    bool release_material(MaterialHandle handle) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool contains(MaterialHandle handle) const noexcept;
    [[nodiscard]] const MaterialRecord* try_get(MaterialHandle handle) const noexcept;
    [[nodiscard]] MaterialHandle resolve(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t material_count() const noexcept;
    [[nodiscard]] MaterialRegistrySummary summary() const noexcept;

  private:
    foundation::HandleMap<MaterialHandleTag, MaterialRecord> records_;
    std::unordered_map<std::string, MaterialHandle> lookup_;
};

} // namespace reaktio::render
