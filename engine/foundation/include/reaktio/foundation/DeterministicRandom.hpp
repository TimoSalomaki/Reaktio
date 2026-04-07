#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace reaktio::foundation {

inline constexpr std::uint64_t k_default_random_seed = 0x5245414b54494f31ull;

class DeterministicRng {
  public:
    explicit DeterministicRng(std::uint64_t seed = k_default_random_seed) noexcept;

    void reseed(std::uint64_t seed) noexcept;

    [[nodiscard]] std::uint64_t initial_seed() const noexcept;
    [[nodiscard]] std::uint64_t generated_values() const noexcept;

    [[nodiscard]] std::uint64_t next_u64() noexcept;
    [[nodiscard]] std::uint32_t next_u32() noexcept;
    [[nodiscard]] std::uint32_t next_u32(std::uint32_t min_inclusive, std::uint32_t max_inclusive) noexcept;
    [[nodiscard]] std::int32_t next_i32(std::int32_t min_inclusive, std::int32_t max_inclusive) noexcept;
    [[nodiscard]] double next_unit_f64() noexcept;
    [[nodiscard]] float next_unit_f32() noexcept;
    [[nodiscard]] bool next_bool() noexcept;

    std::uint64_t state_{};
    std::uint64_t initial_seed_{};
    std::uint64_t generated_values_{};
};

class DeterministicRandomService {
  public:
    explicit DeterministicRandomService(std::uint64_t root_seed = k_default_random_seed) noexcept;

    void reset_streams() noexcept;
    void reseed_root(std::uint64_t root_seed) noexcept;

    [[nodiscard]] std::uint64_t root_seed() const noexcept;
    [[nodiscard]] std::size_t stream_count() const noexcept;
    [[nodiscard]] bool has_stream(std::string_view name) const noexcept;
    [[nodiscard]] std::uint64_t stream_seed(std::string_view name) const noexcept;
    [[nodiscard]] DeterministicRng& stream(std::string_view name);
    [[nodiscard]] const DeterministicRng* find_stream(std::string_view name) const noexcept;

  private:
    struct StreamEntry {
        std::string name;
        std::uint64_t derived_seed{};
        DeterministicRng generator{};
    };

    [[nodiscard]] static std::uint64_t derive_seed(std::uint64_t root_seed, std::string_view name) noexcept;
    [[nodiscard]] StreamEntry* find_entry(std::string_view name) noexcept;
    [[nodiscard]] const StreamEntry* find_entry(std::string_view name) const noexcept;

    std::uint64_t root_seed_{k_default_random_seed};
    std::deque<StreamEntry> streams_;
};

} // namespace reaktio::foundation