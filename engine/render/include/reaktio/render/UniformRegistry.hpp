#pragma once

#include "reaktio/foundation/StrongId.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace reaktio::render {

enum class UniformType : std::uint8_t {
    Float1,
    Float2,
    Float3,
    Float4,
    Mat3,
    Mat4,
    Sampler,
};

struct UniformHandleTag;
using UniformHandle = foundation::StrongId<UniformHandleTag, std::uint32_t>;

struct UniformRecord {
    UniformHandle handle{};
    std::string name;
    UniformType type{UniformType::Float4};
};

using UniformValue = std::variant<
    float,
    std::array<float, 2>,
    std::array<float, 3>,
    std::array<float, 4>,
    std::array<float, 9>,
    std::array<float, 16>,
    std::uint32_t>;

class UniformRegistry {
  public:
    [[nodiscard]] UniformHandle register_uniform(std::string_view name, UniformType type);
    [[nodiscard]] UniformHandle resolve(std::string_view name) const noexcept;
    [[nodiscard]] const UniformRecord* try_get(UniformHandle handle) const noexcept;
    [[nodiscard]] std::size_t uniform_count() const noexcept;
    void clear() noexcept;

  private:
    std::vector<UniformRecord> records_;
    std::unordered_map<std::string, UniformHandle> lookup_;
};

} // namespace reaktio::render
