#include "reaktio/gameplay/ModeConfiguration.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>

namespace reaktio::gameplay {

namespace {

std::string lowercase_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }

    return lowered;
}

bool try_parse_bool(std::string_view value, bool& parsed) noexcept {
    const std::string lowered = lowercase_copy(value);
    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
        parsed = true;
        return true;
    }

    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
        parsed = false;
        return true;
    }

    return false;
}

bool try_parse_int(std::string_view value, std::int64_t& parsed) noexcept {
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc{} && result.ptr == end;
}

bool try_parse_double(std::string_view value, double& parsed) noexcept {
    std::string buffer(value);
    char* parsed_end = nullptr;
    parsed = std::strtod(buffer.c_str(), &parsed_end);
    return parsed_end != nullptr && *parsed_end == '\0';
}

} // namespace

const ModeConfigurationEntry* ModeConfigurationView::find(std::string_view key) const noexcept {
    if (entries_ == nullptr) {
        return nullptr;
    }

    const auto it = std::find_if(entries_->begin(), entries_->end(), [&](const ModeConfigurationEntry& entry) {
        return entry.key == key;
    });
    return it != entries_->end() ? &(*it) : nullptr;
}

std::string_view ModeConfigurationView::get_string(
    std::string_view key,
    std::string_view fallback) const noexcept {
    if (const ModeConfigurationEntry* entry = find(key)) {
        return entry->value;
    }

    return fallback;
}

bool ModeConfigurationView::get_bool(std::string_view key, bool fallback) const noexcept {
    if (const ModeConfigurationEntry* entry = find(key)) {
        bool parsed = fallback;
        if (try_parse_bool(entry->value, parsed)) {
            return parsed;
        }
    }

    return fallback;
}

std::int64_t ModeConfigurationView::get_int(std::string_view key, std::int64_t fallback) const noexcept {
    if (const ModeConfigurationEntry* entry = find(key)) {
        std::int64_t parsed = fallback;
        if (try_parse_int(entry->value, parsed)) {
            return parsed;
        }
    }

    return fallback;
}

double ModeConfigurationView::get_double(std::string_view key, double fallback) const noexcept {
    if (const ModeConfigurationEntry* entry = find(key)) {
        double parsed = fallback;
        if (try_parse_double(entry->value, parsed)) {
            return parsed;
        }
    }

    return fallback;
}

std::span<const ModeConfigurationEntry> ModeConfigurationView::entries() const noexcept {
    if (entries_ == nullptr) {
        return {};
    }

    return std::span<const ModeConfigurationEntry>{entries_->data(), entries_->size()};
}

std::size_t ModeConfigurationView::entry_count() const noexcept {
    return entries_ != nullptr ? entries_->size() : 0u;
}

void ModeConfigurationStore::clear() noexcept {
    entries_by_mode_.clear();
    entry_count_ = 0;
}

void ModeConfigurationStore::set(std::string_view mode_id, std::string_view key, std::string_view value) {
    if (mode_id.empty() || key.empty()) {
        return;
    }

    std::vector<ModeConfigurationEntry>& entries = entries_by_mode_[std::string(mode_id)];
    for (ModeConfigurationEntry& entry : entries) {
        if (entry.key == key) {
            entry.value = std::string(value);
            return;
        }
    }

    entries.push_back(ModeConfigurationEntry{
        .key = std::string(key),
        .value = std::string(value),
    });
    ++entry_count_;
}

bool ModeConfigurationStore::has_mode(std::string_view mode_id) const noexcept {
    return entries_by_mode_.find(std::string(mode_id)) != entries_by_mode_.end();
}

ModeConfigurationView ModeConfigurationStore::view(std::string_view mode_id) const noexcept {
    const auto it = entries_by_mode_.find(std::string(mode_id));
    return it != entries_by_mode_.end() ? ModeConfigurationView{&it->second} : ModeConfigurationView{nullptr};
}

ModeConfigurationSummary ModeConfigurationStore::summary() const noexcept {
    return ModeConfigurationSummary{
        .mode_count = entries_by_mode_.size(),
        .entry_count = entry_count_,
    };
}

} // namespace reaktio::gameplay