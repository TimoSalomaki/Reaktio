#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

inline constexpr std::uint32_t k_save_data_schema_version = 1;
inline constexpr std::string_view k_default_save_profile_id = "default";

enum class SaveSettingValueKind : std::uint8_t {
    Boolean,
    Integer,
    Real,
    Text,
};

struct SaveSettingValue {
    SaveSettingValueKind kind{SaveSettingValueKind::Boolean};
    bool boolean_value{};
    std::int64_t integer_value{};
    double real_value{};
    std::string text_value;
};

struct SaveSetting {
    std::string key;
    SaveSettingValue value;
};

struct SaveSettingsCategory {
    std::string id;
    std::vector<SaveSetting> settings;
};

struct SaveUnlock {
    std::string id;
    std::string source;
    std::uint64_t granted_at_revision{};
};

struct SaveModeStats {
    std::string mode_id;
    std::string song_id;
    std::uint32_t play_count{};
    std::uint32_t clear_count{};
    std::uint32_t fail_count{};
    std::uint64_t best_score{};
    std::uint32_t best_combo{};
    double best_accuracy_ratio{};
    std::string best_grade;
    std::uint64_t total_play_time_ms{};
    std::uint64_t last_played_wall_clock_ns{};
};

struct SaveDataMetadata {
    std::string profile_id{std::string(k_default_save_profile_id)};
    std::uint32_t schema_version{k_save_data_schema_version};
    std::uint64_t document_revision{};
    std::uint64_t last_updated_wall_clock_ns{};
};

struct SaveDataDocument {
    SaveDataMetadata metadata{};
    std::vector<SaveSettingsCategory> settings_categories;
    std::vector<SaveUnlock> unlocks;
    std::vector<SaveModeStats> mode_stats;
};

struct SaveModeStatsResult {
    std::string mode_id;
    std::string song_id;
    bool cleared{};
    bool failed{};
    std::uint64_t score{};
    std::uint32_t combo{};
    double accuracy_ratio{};
    std::string grade;
    std::uint64_t play_time_ms{};
    std::uint64_t wall_clock_ns{};
};

struct SaveDataStatistics {
    std::size_t settings_category_count{};
    std::size_t setting_count{};
    std::size_t unlock_count{};
    std::size_t mode_stats_count{};
    std::uint64_t document_revision{};
    std::uint64_t mutation_count{};
    std::uint64_t rejected_mutation_count{};
};

class SaveDataStore {
  public:
    void reset(SaveDataMetadata metadata);
    void load_document(SaveDataDocument document) noexcept;

    bool upsert_setting(std::string_view category_id, SaveSetting setting, std::uint64_t wall_clock_ns = 0);
    [[nodiscard]] const SaveSetting* find_setting(std::string_view category_id, std::string_view key) const noexcept;

    enum class GrantUnlockResult : std::uint8_t {
        Granted,
        AlreadyGranted,
        Rejected,
    };
    GrantUnlockResult grant_unlock(std::string id, std::string source, std::uint64_t wall_clock_ns = 0);
    [[nodiscard]] bool is_unlocked(std::string_view id) const noexcept;

    bool record_mode_session(const SaveModeStatsResult& result);
    [[nodiscard]] const SaveModeStats* find_mode_stats(std::string_view mode_id, std::string_view song_id) const noexcept;

    [[nodiscard]] const SaveDataDocument& document() const noexcept;
    [[nodiscard]] SaveDataStatistics statistics() const noexcept;

  private:
    bool stamp_mutation(std::uint64_t wall_clock_ns) noexcept;

    SaveDataDocument document_{};
    std::uint64_t mutation_count_{};
    std::uint64_t rejected_mutation_count_{};
};

[[nodiscard]] bool is_valid_save_identifier(std::string_view value) noexcept;
[[nodiscard]] bool is_valid_save_text_value(std::string_view value) noexcept;
[[nodiscard]] std::string serialize_save_data(const SaveDataDocument& document);
[[nodiscard]] bool parse_save_data(std::string_view text, SaveDataDocument& document, std::string* error_message);
[[nodiscard]] bool save_data_documents_equal(const SaveDataDocument& lhs, const SaveDataDocument& rhs) noexcept;

class ISaveDataBackend {
  public:
    virtual ~ISaveDataBackend() = default;

    [[nodiscard]] virtual bool load(SaveDataDocument& document, std::string* error_message) = 0;
    [[nodiscard]] virtual bool save(const SaveDataDocument& document, std::string* error_message) = 0;
    [[nodiscard]] virtual std::string_view backend_id() const noexcept = 0;
};

class InMemorySaveDataBackend final : public ISaveDataBackend {
  public:
    [[nodiscard]] bool load(SaveDataDocument& document, std::string* error_message) override;
    [[nodiscard]] bool save(const SaveDataDocument& document, std::string* error_message) override;
    [[nodiscard]] std::string_view backend_id() const noexcept override;

    [[nodiscard]] const std::string& serialized() const noexcept;
    [[nodiscard]] std::size_t save_count() const noexcept;
    [[nodiscard]] std::size_t load_count() const noexcept;

  private:
    std::string serialized_{};
    bool initialized_{false};
    std::size_t save_count_{};
    std::size_t load_count_{};
};

} // namespace reaktio::gameplay
