#include "reaktio/content/ChartPreview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

enum class CursorMode : std::uint8_t {
    PreviewStart,
    Tick,
    Seconds,
    Samples,
    Beat,
    Bar,
};

struct CursorSpec {
    CursorMode mode{CursorMode::PreviewStart};
    reaktio::rhythm::ChartTick tick{};
    double seconds{};
    reaktio::rhythm::AudioSampleIndex sample_index{};
    reaktio::rhythm::BeatPosition beat{};
    reaktio::rhythm::BarPosition bar{};
};

struct ProgramOptions {
    std::optional<std::filesystem::path> manifest_path;
    std::optional<std::filesystem::path> payload_path;
    std::string chart_id;
    std::string runtime_label;
    bool list_charts{};
    bool interactive{};
    CursorSpec cursor{};
    std::optional<reaktio::rhythm::ChartTick> before_ticks;
    std::optional<reaktio::rhythm::ChartTick> after_ticks;
    std::size_t max_events{16};
};

bool try_parse_int64(std::string_view value, std::int64_t& parsed) noexcept {
    const std::string buffer(value);
    if (buffer.empty()) {
        return false;
    }

    char* end = nullptr;
    const long long converted = std::strtoll(buffer.c_str(), &end, 0);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    parsed = static_cast<std::int64_t>(converted);
    return true;
}

bool try_parse_double(std::string_view value, double& parsed) noexcept {
    const std::string buffer(value);
    if (buffer.empty()) {
        return false;
    }

    char* end = nullptr;
    parsed = std::strtod(buffer.c_str(), &end);
    return end != nullptr && *end == '\0' && std::isfinite(parsed);
}

std::vector<std::string> split_string(std::string_view value, char delimiter) {
    std::vector<std::string> tokens;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        const std::string token = std::string(
            end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start));
        if (!token.empty()) {
            tokens.push_back(token);
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return tokens;
}

bool parse_beat_spec(std::string_view value, reaktio::rhythm::BeatPosition& beat) {
    const std::vector<std::string> tokens = split_string(value, ':');
    if (tokens.empty() || tokens.size() > 2u) {
        return false;
    }

    if (!try_parse_int64(tokens[0], beat.whole_beats)) {
        return false;
    }
    beat.tick_offset_in_beat = 0;
    if (tokens.size() == 2u && !try_parse_int64(tokens[1], beat.tick_offset_in_beat)) {
        return false;
    }
    return true;
}

bool parse_bar_spec(std::string_view value, reaktio::rhythm::BarPosition& bar) {
    const std::vector<std::string> tokens = split_string(value, ':');
    if (tokens.empty() || tokens.size() > 3u) {
        return false;
    }

    if (!try_parse_int64(tokens[0], bar.bar_index)) {
        return false;
    }
    bar.beat_index_in_bar = 0;
    bar.tick_offset_in_beat = 0;
    if (tokens.size() >= 2u) {
        std::int64_t beat_index = 0;
        if (!try_parse_int64(tokens[1], beat_index)) {
            return false;
        }
        bar.beat_index_in_bar = static_cast<std::int32_t>(beat_index);
    }
    if (tokens.size() == 3u && !try_parse_int64(tokens[2], bar.tick_offset_in_beat)) {
        return false;
    }
    return true;
}

std::string position_to_string(const reaktio::rhythm::RhythmPosition& position) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(3);
    stream << "tick=" << position.tick
           << " time=" << position.seconds << "s"
           << " sample=" << position.sample_index
           << " beat=" << position.beat.whole_beats << '+' << position.beat.tick_offset_in_beat << '/' << position.beat.ticks_per_beat
           << " bar=" << position.bar.bar_index << ':' << position.bar.beat_index_in_bar << '+' << position.bar.tick_offset_in_beat;
    return stream.str();
}

std::string event_to_string(const reaktio::content::ChartPreviewEvent& event) {
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(3);
    stream << reaktio::content::to_string(event.relation)
           << ' ' << reaktio::content::to_string(event.kind)
           << ' ' << event.id
           << " delta=" << event.delta_ticks << "t (" << event.delta_seconds << "s)"
           << " start=" << event.start_tick
           << " end=" << event.end_tick_exclusive;
    if (event.lane_index.has_value()) {
        stream << " lane=" << *event.lane_index;
    }
    if (event.channel_index.has_value()) {
        stream << " channel=" << *event.channel_index;
    }
    if (!event.scroll_profile_id.empty()) {
        stream << " scroll=" << event.scroll_profile_id;
    }
    if (!event.judgement_profile_id.empty()) {
        stream << " judge=" << event.judgement_profile_id;
    }
    if (!event.detail.empty()) {
        stream << " detail=" << event.detail;
    }
    return stream.str();
}

void print_help() {
    std::cout
        << "Usage: reaktio_chart_preview [options]\n"
        << "\n"
        << "Selection:\n"
        << "  --manifest <path>         Use a specific cooked chart manifest.\n"
        << "  --chart-id <id>           Select a chart by authored id.\n"
        << "  --runtime-label <label>   Select a chart by runtime label.\n"
        << "  --payload <path>          Load a cooked chart payload directly.\n"
        << "  --list-charts             List available charts and exit.\n"
        << "\n"
        << "Cursor:\n"
        << "  --tick <value>            Scrub to a chart tick.\n"
        << "  --seconds <value>         Scrub to a timeline time in seconds.\n"
        << "  --sample <value>          Scrub to an audio sample index.\n"
        << "  --beat <whole[:offset]>   Scrub to a beat position.\n"
        << "  --bar <bar[:beat[:offset]]> Scrub to a bar position.\n"
        << "\n"
        << "Window:\n"
        << "  --before-ticks <value>    Ticks to include before the cursor.\n"
        << "  --after-ticks <value>     Ticks to include after the cursor.\n"
        << "  --max-events <count>      Maximum events to print from the window.\n"
        << "\n"
        << "Interactive:\n"
        << "  --interactive             Enter a scrub REPL after printing the initial snapshot.\n"
        << "\n"
        << "If no cursor option is supplied, the tool uses the chart preview_start_ms.\n";
}

bool parse_arguments(int argc, char** argv, ProgramOptions& options) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        auto require_value = [&](std::string_view option) -> const char* {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << option << '\n';
                return nullptr;
            }
            return argv[++index];
        };

        if (argument == "--manifest") {
            if (const char* value = require_value(argument)) {
                options.manifest_path = std::filesystem::absolute(std::filesystem::path(value));
                continue;
            }
            return false;
        }
        if (argument == "--chart-id") {
            if (const char* value = require_value(argument)) {
                options.chart_id = value;
                continue;
            }
            return false;
        }
        if (argument == "--runtime-label") {
            if (const char* value = require_value(argument)) {
                options.runtime_label = value;
                continue;
            }
            return false;
        }
        if (argument == "--payload") {
            if (const char* value = require_value(argument)) {
                options.payload_path = std::filesystem::absolute(std::filesystem::path(value));
                continue;
            }
            return false;
        }
        if (argument == "--tick") {
            if (const char* value = require_value(argument)) {
                options.cursor.mode = CursorMode::Tick;
                if (!try_parse_int64(value, options.cursor.tick)) {
                    std::cerr << "Invalid --tick value\n";
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--seconds") {
            if (const char* value = require_value(argument)) {
                options.cursor.mode = CursorMode::Seconds;
                if (!try_parse_double(value, options.cursor.seconds)) {
                    std::cerr << "Invalid --seconds value\n";
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--sample") {
            if (const char* value = require_value(argument)) {
                options.cursor.mode = CursorMode::Samples;
                if (!try_parse_int64(value, options.cursor.sample_index)) {
                    std::cerr << "Invalid --sample value\n";
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--beat") {
            if (const char* value = require_value(argument)) {
                options.cursor.mode = CursorMode::Beat;
                if (!parse_beat_spec(value, options.cursor.beat)) {
                    std::cerr << "Invalid --beat value\n";
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--bar") {
            if (const char* value = require_value(argument)) {
                options.cursor.mode = CursorMode::Bar;
                if (!parse_bar_spec(value, options.cursor.bar)) {
                    std::cerr << "Invalid --bar value\n";
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--before-ticks") {
            if (const char* value = require_value(argument)) {
                reaktio::rhythm::ChartTick parsed = 0;
                if (!try_parse_int64(value, parsed)) {
                    std::cerr << "Invalid --before-ticks value\n";
                    return false;
                }
                options.before_ticks = parsed;
                continue;
            }
            return false;
        }
        if (argument == "--after-ticks") {
            if (const char* value = require_value(argument)) {
                reaktio::rhythm::ChartTick parsed = 0;
                if (!try_parse_int64(value, parsed)) {
                    std::cerr << "Invalid --after-ticks value\n";
                    return false;
                }
                options.after_ticks = parsed;
                continue;
            }
            return false;
        }
        if (argument == "--max-events") {
            if (const char* value = require_value(argument)) {
                std::int64_t parsed = 0;
                if (!try_parse_int64(value, parsed) || parsed <= 0) {
                    std::cerr << "Invalid --max-events value\n";
                    return false;
                }
                options.max_events = static_cast<std::size_t>(parsed);
                continue;
            }
            return false;
        }
        if (argument == "--list-charts") {
            options.list_charts = true;
            continue;
        }
        if (argument == "--interactive") {
            options.interactive = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            print_help();
            return false;
        }

        std::cerr << "Unknown argument: " << argument << '\n';
        return false;
    }

    return true;
}

reaktio::rhythm::ChartTick resolve_cursor_tick(
    const CursorSpec& cursor,
    const reaktio::content::ChartDocument& document,
    const reaktio::rhythm::TempoMap& tempo_map) {
    switch (cursor.mode) {
    case CursorMode::PreviewStart:
        return tempo_map.tick_from_microseconds(document.audio.preview_start_ms * 1000);
    case CursorMode::Tick:
        return std::max<reaktio::rhythm::ChartTick>(0, cursor.tick);
    case CursorMode::Seconds:
        return tempo_map.tick_from_seconds(cursor.seconds);
    case CursorMode::Samples:
        return tempo_map.tick_from_samples(cursor.sample_index);
    case CursorMode::Beat:
        return tempo_map.tick_from_beat(cursor.beat);
    case CursorMode::Bar:
        return tempo_map.tick_from_bar(cursor.bar);
    }

    return 0;
}

reaktio::content::ChartPreviewWindow resolve_window(
    const ProgramOptions& options,
    const reaktio::content::ChartDocument& document) {
    const reaktio::rhythm::ChartTick default_before = static_cast<reaktio::rhythm::ChartTick>(document.tempo_map.config.ticks_per_quarter_note) * 4;
    const reaktio::rhythm::ChartTick default_after = static_cast<reaktio::rhythm::ChartTick>(document.tempo_map.config.ticks_per_quarter_note) * 8;
    return reaktio::content::ChartPreviewWindow{
        .before_ticks = options.before_ticks.value_or(default_before),
        .after_ticks = options.after_ticks.value_or(default_after),
        .max_events = options.max_events,
    };
}

void print_chart_list(std::span<const reaktio::content::CookedChartManifestRecord> records) {
    if (records.empty()) {
        std::cout << "No cooked charts were found in the manifest.\n";
        return;
    }

    for (const reaktio::content::CookedChartManifestRecord& record : records) {
        std::cout << record.authoring_id
                  << " | runtime=" << record.runtime_label
                  << " | payload=" << record.payload_path.string() << '\n';
    }
}

void print_snapshot(
    const reaktio::content::CookedChartManifestRecord* manifest_record,
    const reaktio::content::ChartDocument& document,
    const reaktio::content::ChartPreviewSnapshot& snapshot) {
    std::cout << "Chart: " << document.metadata.display_name << " (" << document.metadata.id << ")\n";
    if (manifest_record != nullptr) {
        std::cout << "Runtime Label: " << manifest_record->runtime_label << "\n";
        std::cout << "Payload: " << manifest_record->payload_path.string() << "\n";
    }
    std::cout << "Audio: clip=" << document.audio.clip_id
              << " preview=" << static_cast<double>(document.audio.preview_start_ms) / 1000.0
              << "s.." << static_cast<double>(document.audio.preview_end_ms) / 1000.0 << "s\n";
    std::cout << "Summary: events=" << snapshot.summary.event_count
              << " interactive=" << snapshot.summary.interactive_cue_count
              << " scroll-profiles=" << snapshot.summary.scroll_profile_count;
    if (!snapshot.summary.empty) {
        std::cout << " range=" << snapshot.summary.first_event_tick << ".." << snapshot.summary.last_event_tick;
    }
    std::cout << "\n";
    std::cout << "Cursor: " << position_to_string(snapshot.cursor) << "\n";
    std::cout << "Window: [" << snapshot.window_start_tick << ", " << snapshot.window_end_tick << "]"
              << " total-events=" << snapshot.total_window_event_count;
    if (snapshot.truncated) {
        std::cout << " (truncated to " << snapshot.events.size() << ')';
    }
    std::cout << "\n";
    if (snapshot.events.empty()) {
        std::cout << "No events in the selected preview window.\n";
        return;
    }

    std::cout << "Events:\n";
    for (const reaktio::content::ChartPreviewEvent& event : snapshot.events) {
        std::cout << "  " << event_to_string(event) << "\n";
    }
}

bool apply_repl_command(
    std::string_view line,
    reaktio::rhythm::ChartTick& cursor_tick,
    reaktio::content::ChartPreviewWindow& window,
    const reaktio::content::ChartDocument& document,
    const reaktio::rhythm::TempoMap& tempo_map) {
    std::istringstream input{std::string(line)};
    std::string command;
    input >> command;
    if (command.empty()) {
        return true;
    }

    if (command == "quit" || command == "exit") {
        return false;
    }
    if (command == "help") {
        std::cout << "Commands: show, tick <n>, seconds <n>, sample <n>, beat <whole[:offset]>, bar <bar[:beat[:offset]]>, next [ticks], prev [ticks], window <before> <after>, max-events <count>, quit\n";
        return true;
    }
    if (command == "show") {
        return true;
    }
    if (command == "tick") {
        std::int64_t parsed = 0;
        if (input >> parsed) {
            cursor_tick = std::max<reaktio::rhythm::ChartTick>(0, parsed);
        }
        return true;
    }
    if (command == "seconds") {
        double parsed = 0.0;
        if (input >> parsed) {
            cursor_tick = tempo_map.tick_from_seconds(parsed);
        }
        return true;
    }
    if (command == "sample") {
        std::int64_t parsed = 0;
        if (input >> parsed) {
            cursor_tick = tempo_map.tick_from_samples(parsed);
        }
        return true;
    }
    if (command == "beat") {
        std::string value;
        if (input >> value) {
            reaktio::rhythm::BeatPosition beat{};
            if (parse_beat_spec(value, beat)) {
                cursor_tick = tempo_map.tick_from_beat(beat);
            }
        }
        return true;
    }
    if (command == "bar") {
        std::string value;
        if (input >> value) {
            reaktio::rhythm::BarPosition bar{};
            if (parse_bar_spec(value, bar)) {
                cursor_tick = tempo_map.tick_from_bar(bar);
            }
        }
        return true;
    }
    if (command == "next" || command == "prev") {
        std::int64_t parsed = document.tempo_map.config.ticks_per_quarter_note;
        input >> parsed;
        const reaktio::rhythm::ChartTick delta = std::max<reaktio::rhythm::ChartTick>(0, parsed);
        if (command == "next") {
            cursor_tick += delta;
        } else {
            cursor_tick = std::max<reaktio::rhythm::ChartTick>(0, cursor_tick - delta);
        }
        return true;
    }
    if (command == "window") {
        std::int64_t before = 0;
        std::int64_t after = 0;
        if (input >> before >> after) {
            window.before_ticks = std::max<reaktio::rhythm::ChartTick>(0, before);
            window.after_ticks = std::max<reaktio::rhythm::ChartTick>(0, after);
        }
        return true;
    }
    if (command == "max-events") {
        std::int64_t count = 0;
        if (input >> count && count > 0) {
            window.max_events = static_cast<std::size_t>(count);
        }
        return true;
    }

    std::cout << "Unknown command. Type 'help' for available commands.\n";
    return true;
}

const reaktio::content::CookedChartManifestRecord* resolve_manifest_record(
    std::span<const reaktio::content::CookedChartManifestRecord> records,
    const ProgramOptions& options) {
    if (!options.chart_id.empty()) {
        const auto it = std::find_if(records.begin(), records.end(), [&](const auto& record) {
            return record.authoring_id == options.chart_id;
        });
        return it != records.end() ? &(*it) : nullptr;
    }
    if (!options.runtime_label.empty()) {
        const auto it = std::find_if(records.begin(), records.end(), [&](const auto& record) {
            return record.runtime_label == options.runtime_label;
        });
        return it != records.end() ? &(*it) : nullptr;
    }
    if (records.size() == 1u) {
        return &records.front();
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    ProgramOptions options{};
    if (!parse_arguments(argc, argv, options)) {
        return 1;
    }

    std::vector<reaktio::content::CookedChartManifestRecord> manifest_records;
    const reaktio::content::CookedChartManifestRecord* manifest_record = nullptr;
    if (options.payload_path) {
        static reaktio::content::CookedChartManifestRecord direct_record{};
        direct_record = {};
        direct_record.authoring_id = options.chart_id.empty() ? std::string("direct") : options.chart_id;
        direct_record.runtime_label = options.runtime_label;
        direct_record.payload_path = *options.payload_path;
        manifest_record = &direct_record;
    } else {
        std::filesystem::path manifest_path;
        if (options.manifest_path.has_value()) {
            manifest_path = *options.manifest_path;
        } else if (const std::optional<std::filesystem::path> default_manifest = reaktio::content::find_default_cooked_chart_manifest_path();
                   default_manifest.has_value()) {
            manifest_path = *default_manifest;
        }
        if (manifest_path.empty()) {
            std::cerr << "Unable to locate a cooked chart manifest. Use --manifest or --payload.\n";
            return 1;
        }

        std::string error_message;
        if (!reaktio::content::load_cooked_chart_manifest(manifest_path, manifest_records, error_message)) {
            std::cerr << error_message << '\n';
            return 1;
        }
        if (options.list_charts) {
            print_chart_list(manifest_records);
            return 0;
        }

        manifest_record = resolve_manifest_record(manifest_records, options);
        if (manifest_record == nullptr) {
            std::cerr << "Unable to resolve a cooked chart from the manifest. Use --list-charts and then select one with --chart-id or --runtime-label.\n";
            return 1;
        }
    }

    reaktio::content::ChartDocument document{};
    std::string error_message;
    if (!reaktio::content::load_cooked_chart_document(manifest_record->payload_path, document, error_message)) {
        std::cerr << error_message << '\n';
        return 1;
    }

    reaktio::rhythm::TempoMap tempo_map;
    if (!tempo_map.rebuild(document.tempo_map)) {
        std::cerr << "Chart tempo map failed validation: " << tempo_map.last_error() << '\n';
        return 1;
    }

    reaktio::rhythm::ChartTick cursor_tick = resolve_cursor_tick(options.cursor, document, tempo_map);
    reaktio::content::ChartPreviewWindow window = resolve_window(options, document);

    while (true) {
        reaktio::content::ChartPreviewSnapshot snapshot{};
        error_message.clear();
        if (!reaktio::content::build_chart_preview_snapshot(document, cursor_tick, window, snapshot, error_message)) {
            std::cerr << error_message << '\n';
            return 1;
        }

        print_snapshot(manifest_record, document, snapshot);
        if (!options.interactive) {
            break;
        }

        std::cout << "preview> ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (!apply_repl_command(line, cursor_tick, window, document, tempo_map)) {
            break;
        }
    }

    return 0;
}