#pragma once

#include "reaktio/foundation/HandleMap.hpp"
#include "reaktio/foundation/StrongId.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::render {

struct ShaderProgramHandleTag;
using ShaderProgramHandle = foundation::StrongId<ShaderProgramHandleTag, std::uint64_t>;

struct ShaderProgramRecord {
    ShaderProgramHandle handle{};
    std::string name;
    bool loaded{false};
};

struct ShaderProgramRegistrySummary {
    std::size_t program_count{};
    std::size_t loaded_count{};
};

class ShaderProgramRegistry {
  public:
    [[nodiscard]] ShaderProgramHandle register_program(std::string_view name);
    bool mark_loaded(ShaderProgramHandle handle) noexcept;
    bool release_program(ShaderProgramHandle handle) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool contains(ShaderProgramHandle handle) const noexcept;
    [[nodiscard]] const ShaderProgramRecord* try_get(ShaderProgramHandle handle) const noexcept;
    [[nodiscard]] ShaderProgramHandle resolve(std::string_view name) const noexcept;
    [[nodiscard]] std::size_t program_count() const noexcept;
    [[nodiscard]] ShaderProgramRegistrySummary summary() const noexcept;

  private:
    foundation::HandleMap<ShaderProgramHandleTag, ShaderProgramRecord> records_;
    std::unordered_map<std::string, ShaderProgramHandle> lookup_;
    std::size_t loaded_count_{};
};

} // namespace reaktio::render
