#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::gameplay {

struct ModeConfigurationEntry {
    std::string key;
    std::string value;
};

struct ModeConfigurationSummary {
    std::size_t mode_count{};
    std::size_t entry_count{};
};

class ModeConfigurationView {
  public:
    [[nodiscard]] const ModeConfigurationEntry* find(std::string_view key) const noexcept;
    [[nodiscard]] std::string_view get_string(
        std::string_view key,
        std::string_view fallback = {}) const noexcept;
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback) const noexcept;
    [[nodiscard]] std::int64_t get_int(std::string_view key, std::int64_t fallback) const noexcept;
    [[nodiscard]] double get_double(std::string_view key, double fallback) const noexcept;
    [[nodiscard]] std::span<const ModeConfigurationEntry> entries() const noexcept;
    [[nodiscard]] std::size_t entry_count() const noexcept;

  private:
    friend class ModeConfigurationStore;

    explicit ModeConfigurationView(const std::vector<ModeConfigurationEntry>* entries) noexcept
        : entries_(entries) {}

    const std::vector<ModeConfigurationEntry>* entries_{};
};

class ModeConfigurationStore {
  public:
    void clear() noexcept;
    void set(std::string_view mode_id, std::string_view key, std::string_view value);

    [[nodiscard]] bool has_mode(std::string_view mode_id) const noexcept;
    [[nodiscard]] ModeConfigurationView view(std::string_view mode_id) const noexcept;
    [[nodiscard]] ModeConfigurationSummary summary() const noexcept;

  private:
    std::unordered_map<std::string, std::vector<ModeConfigurationEntry>> entries_by_mode_;
    std::size_t entry_count_{};
};

} // namespace reaktio::gameplay