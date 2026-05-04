#include "reaktio/gameplay/SaveData.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

namespace reaktio::gameplay {

namespace {

constexpr char k_token_separator = '|';
constexpr char k_kv_separator = '=';

bool is_disallowed_save_character(char character) noexcept {
    if (character == k_token_separator || character == k_kv_separator) {
        return true;
    }
    if (character == '\n' || character == '\r' || character == '\0') {
        return true;
    }
    if (static_cast<unsigned char>(character) < 0x20 || character == 0x7f) {
        return true;
    }
    return false;
}

bool parse_int64(std::string_view text, std::int64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_uint64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_uint32(std::string_view text, std::uint32_t& out) noexcept {
    std::uint64_t parsed = 0;
    if (!parse_uint64(text, parsed) || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_double(std::string_view text, double& out) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto result = std::from_chars(begin, end, out);
    return result.ec == std::errc{} && result.ptr == end;
}

bool parse_bool(std::string_view text, bool& out) noexcept {
    if (text == "0" || text == "false") {
        out = false;
        return true;
    }
    if (text == "1" || text == "true") {
        out = true;
        return true;
    }
    return false;
}

void split_tokens(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    std::size_t start = 0;
    for (std::size_t index = 0; index <= line.size(); ++index) {
        if (index == line.size() || line[index] == k_token_separator) {
            out.push_back(line.substr(start, index - start));
            start = index + 1;
        }
    }
}

bool split_kv(std::string_view token, std::string_view& key, std::string_view& value) noexcept {
    const std::size_t equal = token.find(k_kv_separator);
    if (equal == std::string_view::npos) {
        return false;
    }
    key = token.substr(0, equal);
    value = token.substr(equal + 1);
    return true;
}

std::string format_double(double value) {
    std::array<char, 64> buffer{};
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        return std::string("0");
    }
    return std::string(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

std::string_view setting_kind_to_string(SaveSettingValueKind kind) noexcept {
    switch (kind) {
    case SaveSettingValueKind::Boolean:
        return "bool";
    case SaveSettingValueKind::Integer:
        return "int";
    case SaveSettingValueKind::Real:
        return "real";
    case SaveSettingValueKind::Text:
        return "text";
    }
    return "bool";
}

bool parse_setting_kind(std::string_view text, SaveSettingValueKind& out) noexcept {
    if (text == "bool") {
        out = SaveSettingValueKind::Boolean;
        return true;
    }
    if (text == "int") {
        out = SaveSettingValueKind::Integer;
        return true;
    }
    if (text == "real") {
        out = SaveSettingValueKind::Real;
        return true;
    }
    if (text == "text") {
        out = SaveSettingValueKind::Text;
        return true;
    }
    return false;
}

void canonicalize_document(SaveDataDocument& document) {
    std::sort(
        document.settings_categories.begin(),
        document.settings_categories.end(),
        [](const SaveSettingsCategory& lhs, const SaveSettingsCategory& rhs) noexcept {
            return lhs.id < rhs.id;
        });
    for (SaveSettingsCategory& category : document.settings_categories) {
        std::sort(
            category.settings.begin(),
            category.settings.end(),
            [](const SaveSetting& lhs, const SaveSetting& rhs) noexcept {
                return lhs.key < rhs.key;
            });
    }
    std::sort(
        document.unlocks.begin(),
        document.unlocks.end(),
        [](const SaveUnlock& lhs, const SaveUnlock& rhs) noexcept {
            return lhs.id < rhs.id;
        });
    std::sort(
        document.mode_stats.begin(),
        document.mode_stats.end(),
        [](const SaveModeStats& lhs, const SaveModeStats& rhs) noexcept {
            if (lhs.mode_id != rhs.mode_id) {
                return lhs.mode_id < rhs.mode_id;
            }
            return lhs.song_id < rhs.song_id;
        });
}

bool settings_category_equal(const SaveSettingsCategory& lhs, const SaveSettingsCategory& rhs) noexcept {
    if (lhs.id != rhs.id || lhs.settings.size() != rhs.settings.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.settings.size(); ++index) {
        const SaveSetting& a = lhs.settings[index];
        const SaveSetting& b = rhs.settings[index];
        if (a.key != b.key || a.value.kind != b.value.kind) {
            return false;
        }
        switch (a.value.kind) {
        case SaveSettingValueKind::Boolean:
            if (a.value.boolean_value != b.value.boolean_value) {
                return false;
            }
            break;
        case SaveSettingValueKind::Integer:
            if (a.value.integer_value != b.value.integer_value) {
                return false;
            }
            break;
        case SaveSettingValueKind::Real:
            if (a.value.real_value != b.value.real_value) {
                return false;
            }
            break;
        case SaveSettingValueKind::Text:
            if (a.value.text_value != b.value.text_value) {
                return false;
            }
            break;
        }
    }
    return true;
}

bool unlock_equal(const SaveUnlock& lhs, const SaveUnlock& rhs) noexcept {
    return lhs.id == rhs.id && lhs.source == rhs.source && lhs.granted_at_revision == rhs.granted_at_revision;
}

bool mode_stats_equal(const SaveModeStats& lhs, const SaveModeStats& rhs) noexcept {
    return lhs.mode_id == rhs.mode_id && lhs.song_id == rhs.song_id && lhs.play_count == rhs.play_count &&
        lhs.clear_count == rhs.clear_count && lhs.fail_count == rhs.fail_count &&
        lhs.best_score == rhs.best_score && lhs.best_combo == rhs.best_combo &&
        lhs.best_accuracy_ratio == rhs.best_accuracy_ratio && lhs.best_grade == rhs.best_grade &&
        lhs.total_play_time_ms == rhs.total_play_time_ms &&
        lhs.last_played_wall_clock_ns == rhs.last_played_wall_clock_ns;
}

} // namespace

bool is_valid_save_identifier(std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (char character : value) {
        if (is_disallowed_save_character(character) || character == ' ') {
            return false;
        }
    }
    return true;
}

bool is_valid_save_text_value(std::string_view value) noexcept {
    for (char character : value) {
        if (is_disallowed_save_character(character)) {
            return false;
        }
    }
    return true;
}

void SaveDataStore::reset(SaveDataMetadata metadata) {
    document_ = SaveDataDocument{};
    document_.metadata = std::move(metadata);
    if (document_.metadata.profile_id.empty()) {
        document_.metadata.profile_id = std::string(k_default_save_profile_id);
    }
    if (document_.metadata.schema_version == 0) {
        document_.metadata.schema_version = k_save_data_schema_version;
    }
    mutation_count_ = 0;
    rejected_mutation_count_ = 0;
}

void SaveDataStore::load_document(SaveDataDocument document) noexcept {
    document_ = std::move(document);
    canonicalize_document(document_);
    if (document_.metadata.schema_version == 0) {
        document_.metadata.schema_version = k_save_data_schema_version;
    }
    if (document_.metadata.profile_id.empty()) {
        document_.metadata.profile_id = std::string(k_default_save_profile_id);
    }
}

bool SaveDataStore::stamp_mutation(std::uint64_t wall_clock_ns) noexcept {
    ++document_.metadata.document_revision;
    document_.metadata.last_updated_wall_clock_ns = wall_clock_ns;
    ++mutation_count_;
    return true;
}

bool SaveDataStore::upsert_setting(std::string_view category_id, SaveSetting setting, std::uint64_t wall_clock_ns) {
    if (!is_valid_save_identifier(category_id) || !is_valid_save_identifier(setting.key)) {
        ++rejected_mutation_count_;
        return false;
    }
    if (setting.value.kind == SaveSettingValueKind::Text && !is_valid_save_text_value(setting.value.text_value)) {
        ++rejected_mutation_count_;
        return false;
    }

    SaveSettingsCategory* category = nullptr;
    for (SaveSettingsCategory& candidate : document_.settings_categories) {
        if (candidate.id == category_id) {
            category = &candidate;
            break;
        }
    }
    if (category == nullptr) {
        document_.settings_categories.push_back(SaveSettingsCategory{.id = std::string(category_id)});
        category = &document_.settings_categories.back();
    }

    for (SaveSetting& existing : category->settings) {
        if (existing.key == setting.key) {
            existing.value = std::move(setting.value);
            stamp_mutation(wall_clock_ns);
            canonicalize_document(document_);
            return true;
        }
    }
    category->settings.push_back(std::move(setting));
    stamp_mutation(wall_clock_ns);
    canonicalize_document(document_);
    return true;
}

const SaveSetting* SaveDataStore::find_setting(std::string_view category_id, std::string_view key) const noexcept {
    for (const SaveSettingsCategory& category : document_.settings_categories) {
        if (category.id != category_id) {
            continue;
        }
        for (const SaveSetting& setting : category.settings) {
            if (setting.key == key) {
                return &setting;
            }
        }
    }
    return nullptr;
}

SaveDataStore::GrantUnlockResult SaveDataStore::grant_unlock(
    std::string id, std::string source, std::uint64_t wall_clock_ns) {
    if (!is_valid_save_identifier(id) || !is_valid_save_identifier(source)) {
        ++rejected_mutation_count_;
        return GrantUnlockResult::Rejected;
    }
    for (const SaveUnlock& existing : document_.unlocks) {
        if (existing.id == id) {
            return GrantUnlockResult::AlreadyGranted;
        }
    }

    document_.unlocks.push_back(SaveUnlock{
        .id = std::move(id),
        .source = std::move(source),
        .granted_at_revision = document_.metadata.document_revision + 1,
    });
    stamp_mutation(wall_clock_ns);
    canonicalize_document(document_);
    return GrantUnlockResult::Granted;
}

bool SaveDataStore::is_unlocked(std::string_view id) const noexcept {
    for (const SaveUnlock& unlock : document_.unlocks) {
        if (unlock.id == id) {
            return true;
        }
    }
    return false;
}

bool SaveDataStore::record_mode_session(const SaveModeStatsResult& result) {
    if (!is_valid_save_identifier(result.mode_id) || !is_valid_save_identifier(result.song_id)) {
        ++rejected_mutation_count_;
        return false;
    }
    if (!result.grade.empty() && !is_valid_save_text_value(result.grade)) {
        ++rejected_mutation_count_;
        return false;
    }

    SaveModeStats* stats = nullptr;
    for (SaveModeStats& candidate : document_.mode_stats) {
        if (candidate.mode_id == result.mode_id && candidate.song_id == result.song_id) {
            stats = &candidate;
            break;
        }
    }
    if (stats == nullptr) {
        document_.mode_stats.push_back(SaveModeStats{
            .mode_id = result.mode_id,
            .song_id = result.song_id,
        });
        stats = &document_.mode_stats.back();
    }

    ++stats->play_count;
    if (result.cleared) {
        ++stats->clear_count;
    }
    if (result.failed) {
        ++stats->fail_count;
    }
    if (result.score > stats->best_score) {
        stats->best_score = result.score;
    }
    if (result.combo > stats->best_combo) {
        stats->best_combo = result.combo;
    }
    if (result.accuracy_ratio > stats->best_accuracy_ratio) {
        stats->best_accuracy_ratio = result.accuracy_ratio;
        if (!result.grade.empty()) {
            stats->best_grade = result.grade;
        }
    } else if (stats->best_grade.empty() && !result.grade.empty()) {
        stats->best_grade = result.grade;
    }
    stats->total_play_time_ms += result.play_time_ms;
    stats->last_played_wall_clock_ns = result.wall_clock_ns;

    stamp_mutation(result.wall_clock_ns);
    canonicalize_document(document_);
    return true;
}

const SaveModeStats* SaveDataStore::find_mode_stats(std::string_view mode_id, std::string_view song_id) const noexcept {
    for (const SaveModeStats& stats : document_.mode_stats) {
        if (stats.mode_id == mode_id && stats.song_id == song_id) {
            return &stats;
        }
    }
    return nullptr;
}

const SaveDataDocument& SaveDataStore::document() const noexcept {
    return document_;
}

SaveDataStatistics SaveDataStore::statistics() const noexcept {
    SaveDataStatistics stats{};
    stats.settings_category_count = document_.settings_categories.size();
    for (const SaveSettingsCategory& category : document_.settings_categories) {
        stats.setting_count += category.settings.size();
    }
    stats.unlock_count = document_.unlocks.size();
    stats.mode_stats_count = document_.mode_stats.size();
    stats.document_revision = document_.metadata.document_revision;
    stats.mutation_count = mutation_count_;
    stats.rejected_mutation_count = rejected_mutation_count_;
    return stats;
}

std::string serialize_save_data(const SaveDataDocument& document) {
    SaveDataDocument canonical = document;
    canonicalize_document(canonical);

    std::ostringstream stream;
    stream << "save_data" << k_token_separator << canonical.metadata.schema_version << '\n';
    stream << "meta"
           << k_token_separator << "profile=" << canonical.metadata.profile_id
           << k_token_separator << "revision=" << canonical.metadata.document_revision
           << k_token_separator << "updated_ns=" << canonical.metadata.last_updated_wall_clock_ns << '\n';

    for (const SaveSettingsCategory& category : canonical.settings_categories) {
        for (const SaveSetting& setting : category.settings) {
            stream << "setting"
                   << k_token_separator << "category=" << category.id
                   << k_token_separator << "key=" << setting.key
                   << k_token_separator << "kind=" << setting_kind_to_string(setting.value.kind);
            stream << k_token_separator << "value=";
            switch (setting.value.kind) {
            case SaveSettingValueKind::Boolean:
                stream << (setting.value.boolean_value ? '1' : '0');
                break;
            case SaveSettingValueKind::Integer:
                stream << setting.value.integer_value;
                break;
            case SaveSettingValueKind::Real:
                stream << format_double(setting.value.real_value);
                break;
            case SaveSettingValueKind::Text:
                stream << setting.value.text_value;
                break;
            }
            stream << '\n';
        }
    }

    for (const SaveUnlock& unlock : canonical.unlocks) {
        stream << "unlock"
               << k_token_separator << "id=" << unlock.id
               << k_token_separator << "source=" << unlock.source
               << k_token_separator << "revision=" << unlock.granted_at_revision << '\n';
    }

    for (const SaveModeStats& stats : canonical.mode_stats) {
        stream << "stats"
               << k_token_separator << "mode_id=" << stats.mode_id
               << k_token_separator << "song_id=" << stats.song_id
               << k_token_separator << "plays=" << stats.play_count
               << k_token_separator << "clears=" << stats.clear_count
               << k_token_separator << "fails=" << stats.fail_count
               << k_token_separator << "score=" << stats.best_score
               << k_token_separator << "combo=" << stats.best_combo
               << k_token_separator << "accuracy=" << format_double(stats.best_accuracy_ratio)
               << k_token_separator << "grade=" << stats.best_grade
               << k_token_separator << "time_ms=" << stats.total_play_time_ms
               << k_token_separator << "last_ns=" << stats.last_played_wall_clock_ns << '\n';
    }

    return stream.str();
}

namespace {

bool parse_meta_line(const std::vector<std::string_view>& tokens, SaveDataMetadata& metadata, std::string* error) {
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        std::string_view key;
        std::string_view value;
        if (!split_kv(tokens[index], key, value)) {
            continue;
        }
        if (key == "profile") {
            metadata.profile_id = std::string(value);
        } else if (key == "revision") {
            std::uint64_t parsed = 0;
            if (!parse_uint64(value, parsed)) {
                if (error != nullptr) {
                    *error = "save_data: invalid meta revision";
                }
                return false;
            }
            metadata.document_revision = parsed;
        } else if (key == "updated_ns") {
            std::uint64_t parsed = 0;
            if (!parse_uint64(value, parsed)) {
                if (error != nullptr) {
                    *error = "save_data: invalid meta updated_ns";
                }
                return false;
            }
            metadata.last_updated_wall_clock_ns = parsed;
        }
    }
    return true;
}

bool parse_setting_line(
    const std::vector<std::string_view>& tokens,
    SaveDataDocument& document,
    std::string* error) {
    std::string_view category_id;
    std::string_view key;
    SaveSettingValueKind kind = SaveSettingValueKind::Boolean;
    bool kind_set = false;
    std::string_view raw_value;
    bool value_set = false;

    for (std::size_t index = 1; index < tokens.size(); ++index) {
        std::string_view token_key;
        std::string_view token_value;
        if (!split_kv(tokens[index], token_key, token_value)) {
            continue;
        }
        if (token_key == "category") {
            category_id = token_value;
        } else if (token_key == "key") {
            key = token_value;
        } else if (token_key == "kind") {
            if (!parse_setting_kind(token_value, kind)) {
                if (error != nullptr) {
                    *error = "save_data: unknown setting kind";
                }
                return false;
            }
            kind_set = true;
        } else if (token_key == "value") {
            raw_value = token_value;
            value_set = true;
        }
    }
    if (category_id.empty() || key.empty() || !kind_set || !value_set) {
        return true; // tolerate partial; forward-compat.
    }

    SaveSetting setting{};
    setting.key = std::string(key);
    setting.value.kind = kind;
    switch (kind) {
    case SaveSettingValueKind::Boolean:
        if (!parse_bool(raw_value, setting.value.boolean_value)) {
            if (error != nullptr) {
                *error = "save_data: invalid bool value";
            }
            return false;
        }
        break;
    case SaveSettingValueKind::Integer:
        if (!parse_int64(raw_value, setting.value.integer_value)) {
            if (error != nullptr) {
                *error = "save_data: invalid int value";
            }
            return false;
        }
        break;
    case SaveSettingValueKind::Real:
        if (!parse_double(raw_value, setting.value.real_value)) {
            if (error != nullptr) {
                *error = "save_data: invalid real value";
            }
            return false;
        }
        break;
    case SaveSettingValueKind::Text:
        setting.value.text_value = std::string(raw_value);
        break;
    }

    SaveSettingsCategory* category = nullptr;
    for (SaveSettingsCategory& existing : document.settings_categories) {
        if (existing.id == category_id) {
            category = &existing;
            break;
        }
    }
    if (category == nullptr) {
        document.settings_categories.push_back(SaveSettingsCategory{.id = std::string(category_id)});
        category = &document.settings_categories.back();
    }
    bool replaced = false;
    for (SaveSetting& existing : category->settings) {
        if (existing.key == setting.key) {
            existing.value = setting.value;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        category->settings.push_back(std::move(setting));
    }
    return true;
}

bool parse_unlock_line(
    const std::vector<std::string_view>& tokens,
    SaveDataDocument& document,
    std::string* error) {
    SaveUnlock unlock{};
    bool id_set = false;
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        std::string_view token_key;
        std::string_view token_value;
        if (!split_kv(tokens[index], token_key, token_value)) {
            continue;
        }
        if (token_key == "id") {
            unlock.id = std::string(token_value);
            id_set = true;
        } else if (token_key == "source") {
            unlock.source = std::string(token_value);
        } else if (token_key == "revision") {
            std::uint64_t parsed = 0;
            if (!parse_uint64(token_value, parsed)) {
                if (error != nullptr) {
                    *error = "save_data: invalid unlock revision";
                }
                return false;
            }
            unlock.granted_at_revision = parsed;
        }
    }
    if (!id_set) {
        return true;
    }
    document.unlocks.push_back(std::move(unlock));
    return true;
}

bool parse_stats_line(
    const std::vector<std::string_view>& tokens,
    SaveDataDocument& document,
    std::string* error) {
    SaveModeStats stats{};
    bool keyed = false;
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        std::string_view token_key;
        std::string_view token_value;
        if (!split_kv(tokens[index], token_key, token_value)) {
            continue;
        }
        if (token_key == "mode_id") {
            stats.mode_id = std::string(token_value);
            keyed = !stats.mode_id.empty() && !stats.song_id.empty();
        } else if (token_key == "song_id") {
            stats.song_id = std::string(token_value);
            keyed = !stats.mode_id.empty() && !stats.song_id.empty();
        } else if (token_key == "plays") {
            if (!parse_uint32(token_value, stats.play_count)) {
                if (error != nullptr) {
                    *error = "save_data: invalid stats plays";
                }
                return false;
            }
        } else if (token_key == "clears") {
            if (!parse_uint32(token_value, stats.clear_count)) {
                return false;
            }
        } else if (token_key == "fails") {
            if (!parse_uint32(token_value, stats.fail_count)) {
                return false;
            }
        } else if (token_key == "score") {
            if (!parse_uint64(token_value, stats.best_score)) {
                return false;
            }
        } else if (token_key == "combo") {
            if (!parse_uint32(token_value, stats.best_combo)) {
                return false;
            }
        } else if (token_key == "accuracy") {
            if (!parse_double(token_value, stats.best_accuracy_ratio)) {
                return false;
            }
        } else if (token_key == "grade") {
            stats.best_grade = std::string(token_value);
        } else if (token_key == "time_ms") {
            if (!parse_uint64(token_value, stats.total_play_time_ms)) {
                return false;
            }
        } else if (token_key == "last_ns") {
            if (!parse_uint64(token_value, stats.last_played_wall_clock_ns)) {
                return false;
            }
        }
    }
    if (!keyed) {
        return true;
    }
    document.mode_stats.push_back(std::move(stats));
    return true;
}

} // namespace

bool parse_save_data(std::string_view text, SaveDataDocument& document, std::string* error_message) {
    document = SaveDataDocument{};
    bool header_seen = false;
    std::vector<std::string_view> tokens;

    std::size_t line_start = 0;
    while (line_start <= text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = text.size();
        }
        std::string_view line = text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!line.empty()) {
            split_tokens(line, tokens);
            if (tokens.empty() || tokens[0].empty()) {
                // skip
            } else if (!header_seen) {
                if (tokens[0] != "save_data") {
                    if (error_message != nullptr) {
                        *error_message = "save_data: missing or malformed header";
                    }
                    return false;
                }
                if (tokens.size() < 2) {
                    if (error_message != nullptr) {
                        *error_message = "save_data: header missing schema version";
                    }
                    return false;
                }
                std::uint32_t schema = 0;
                if (!parse_uint32(tokens[1], schema)) {
                    if (error_message != nullptr) {
                        *error_message = "save_data: header schema version not an integer";
                    }
                    return false;
                }
                document.metadata.schema_version = schema;
                header_seen = true;
            } else if (tokens[0] == "meta") {
                if (!parse_meta_line(tokens, document.metadata, error_message)) {
                    return false;
                }
            } else if (tokens[0] == "setting") {
                if (!parse_setting_line(tokens, document, error_message)) {
                    return false;
                }
            } else if (tokens[0] == "unlock") {
                if (!parse_unlock_line(tokens, document, error_message)) {
                    return false;
                }
            } else if (tokens[0] == "stats") {
                if (!parse_stats_line(tokens, document, error_message)) {
                    return false;
                }
            }
            // Unknown line types are intentionally ignored for forward-compat.
        }

        line_start = line_end + 1;
        if (line_end == text.size()) {
            break;
        }
    }

    if (!header_seen) {
        if (error_message != nullptr) {
            *error_message = "save_data: empty document";
        }
        return false;
    }
    if (document.metadata.profile_id.empty()) {
        document.metadata.profile_id = std::string(k_default_save_profile_id);
    }
    if (document.metadata.schema_version == 0) {
        document.metadata.schema_version = k_save_data_schema_version;
    }
    canonicalize_document(document);
    return true;
}

bool save_data_documents_equal(const SaveDataDocument& lhs, const SaveDataDocument& rhs) noexcept {
    if (lhs.metadata.profile_id != rhs.metadata.profile_id ||
        lhs.metadata.schema_version != rhs.metadata.schema_version ||
        lhs.metadata.document_revision != rhs.metadata.document_revision ||
        lhs.metadata.last_updated_wall_clock_ns != rhs.metadata.last_updated_wall_clock_ns) {
        return false;
    }
    if (lhs.settings_categories.size() != rhs.settings_categories.size() ||
        lhs.unlocks.size() != rhs.unlocks.size() ||
        lhs.mode_stats.size() != rhs.mode_stats.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.settings_categories.size(); ++index) {
        if (!settings_category_equal(lhs.settings_categories[index], rhs.settings_categories[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < lhs.unlocks.size(); ++index) {
        if (!unlock_equal(lhs.unlocks[index], rhs.unlocks[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < lhs.mode_stats.size(); ++index) {
        if (!mode_stats_equal(lhs.mode_stats[index], rhs.mode_stats[index])) {
            return false;
        }
    }
    return true;
}

bool InMemorySaveDataBackend::load(SaveDataDocument& document, std::string* error_message) {
    ++load_count_;
    if (!initialized_) {
        document = SaveDataDocument{};
        document.metadata.schema_version = k_save_data_schema_version;
        document.metadata.profile_id = std::string(k_default_save_profile_id);
        return true;
    }
    return parse_save_data(serialized_, document, error_message);
}

bool InMemorySaveDataBackend::save(const SaveDataDocument& document, std::string* error_message) {
    serialized_ = serialize_save_data(document);
    initialized_ = true;
    ++save_count_;
    (void)error_message;
    return true;
}

std::string_view InMemorySaveDataBackend::backend_id() const noexcept {
    return "in-memory";
}

const std::string& InMemorySaveDataBackend::serialized() const noexcept {
    return serialized_;
}

std::size_t InMemorySaveDataBackend::save_count() const noexcept {
    return save_count_;
}

std::size_t InMemorySaveDataBackend::load_count() const noexcept {
    return load_count_;
}

} // namespace reaktio::gameplay
