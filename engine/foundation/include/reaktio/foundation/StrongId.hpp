#pragma once

#include <cstdint>
#include <type_traits>

namespace reaktio::foundation {

template <typename Tag, typename Underlying = std::uint32_t>
class StrongId {
    static_assert(std::is_integral_v<Underlying>, "StrongId requires an integral underlying type.");

  public:
    using underlying_type = Underlying;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(Underlying value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Underlying value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool valid() const noexcept { return value_ != Underlying{}; }

    friend constexpr bool operator==(StrongId lhs, StrongId rhs) noexcept = default;
    friend constexpr auto operator<=>(StrongId lhs, StrongId rhs) noexcept = default;

  private:
    Underlying value_{};
};

} // namespace reaktio::foundation