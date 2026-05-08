#include "reaktio/content/ContentValidation.hpp"
#include "reaktio/foundation/CrashSafeLog.hpp"

#include "reaktio/tools/AssetBrowserModel.hpp"
#include "reaktio/tools/InspectorPanel.hpp"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ProgramOptions {
    reaktio::content::ContentValidationOptions validation;
    bool raw_root_set{};
    bool cooked_root_set{};
    bool show_help{};
    bool show_asset_browser{};
};

std::string trim_copy(std::string_view value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(start, end - start));
}

std::string lowercase_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(character)));
    }
    return lowered;
}

std::vector<std::string> split_checks(std::string_view value) {
    std::vector<std::string> checks;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(',', start);
        const std::string check = lowercase_copy(trim_copy(
            end == std::string_view::npos ? value.substr(start) : value.substr(start, end - start)));
        if (!check.empty()) {
            checks.push_back(check);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return checks;
}

bool apply_check_list(std::string_view value, reaktio::content::ContentValidationOptions& options) {
    const std::vector<std::string> checks = split_checks(value);
    if (checks.empty()) {
        std::cerr << "--checks requires at least one check name\n";
        return false;
    }

    options.validate_charts = false;
    options.validate_timing = false;
    options.validate_missing_assets = false;

    for (const std::string& check : checks) {
        if (check == "all") {
            options.validate_charts = true;
            options.validate_timing = true;
            options.validate_missing_assets = true;
            continue;
        }
        if (check == "charts") {
            options.validate_charts = true;
            continue;
        }
        if (check == "timing") {
            options.validate_timing = true;
            continue;
        }
        if (check == "assets" || check == "missing-assets") {
            options.validate_missing_assets = true;
            continue;
        }

        std::cerr << "Unknown validation check: " << check << '\n';
        return false;
    }

    return true;
}

void print_help() {
    std::cout
        << "Usage: reaktio_content_validator [options]\n"
        << "\n"
        << "Roots:\n"
        << "  --raw-root <path>       Use a specific raw content root.\n"
        << "  --cooked-root <path>    Use a specific cooked content root.\n"
        << "\n"
        << "Checks:\n"
        << "  --checks <list>         Comma-separated checks: all, charts, timing, assets.\n"
        << "                          Defaults to all.\n"
        << "\n"
        << "Other:\n"
        << "  --asset-browser         Print the asset browser snapshot (cooked chart\n"
        << "                          manifest + dependency graph) after validation.\n"
        << "  -h, --help              Show this help text.\n";
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

        if (argument == "--raw-root") {
            if (const char* value = require_value(argument)) {
                options.validation.paths.raw_root = std::filesystem::absolute(std::filesystem::path(value));
                options.raw_root_set = true;
                continue;
            }
            return false;
        }
        if (argument == "--cooked-root") {
            if (const char* value = require_value(argument)) {
                options.validation.paths.cooked_root = std::filesystem::absolute(std::filesystem::path(value));
                options.cooked_root_set = true;
                continue;
            }
            return false;
        }
        if (argument == "--checks") {
            if (const char* value = require_value(argument)) {
                if (!apply_check_list(value, options.validation)) {
                    return false;
                }
                continue;
            }
            return false;
        }
        if (argument == "--asset-browser") {
            options.show_asset_browser = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            options.show_help = true;
            return true;
        }

        std::cerr << "Unknown argument: " << argument << '\n';
        return false;
    }

    return true;
}

void print_issue(const reaktio::content::ContentCookIssue& issue) {
    std::cout << reaktio::content::to_string(issue.severity) << ": ";
    if (!issue.source_path.empty()) {
        std::cout << issue.source_path.string();
        if (issue.line > 0) {
            std::cout << ':' << issue.line;
        }
        std::cout << ": ";
    }
    std::cout << issue.message << '\n';
}

void print_summary(const reaktio::content::ContentValidationSummary& summary) {
    std::cout << "Validated charts=" << summary.validated_chart_count << '/' << summary.chart_manifest_entry_count
              << " events=" << summary.total_chart_events
              << " interactive=" << summary.total_interactive_cues
              << " audio-clips=" << summary.audio_clip_count
              << " referenced-audio=" << summary.referenced_audio_clip_count
              << " missing-audio=" << summary.missing_audio_clip_reference_count
              << " render-assets=" << summary.render_asset_count
              << " warnings=" << summary.warning_count
              << " errors=" << summary.error_count << '\n';
}

} // namespace

int main(int argc, char** argv) {
    ProgramOptions options{};
    if (!parse_arguments(argc, argv, options)) {
        return 1;
    }
    if (options.show_help) {
        print_help();
        return 0;
    }

    if (!options.raw_root_set || !options.cooked_root_set) {
        const std::optional<reaktio::content::ContentRootPaths> default_paths = reaktio::content::find_default_content_roots();
        if (!default_paths) {
            std::cerr << "Unable to locate content/raw and content/cooked roots.\n";
            return 1;
        }
        if (!options.raw_root_set) {
            options.validation.paths.raw_root = default_paths->raw_root;
        }
        if (!options.cooked_root_set) {
            options.validation.paths.cooked_root = default_paths->cooked_root;
        }
    }

    reaktio::foundation::CrashSafeLog log;
    (void)log.open_file(std::filesystem::current_path() / "logs" / "reaktio-content-validator.log");

    reaktio::content::ContentValidator validator;
    const bool valid = validator.validate_all(options.validation, log);
    for (const reaktio::content::ContentCookIssue& issue : validator.issues()) {
        print_issue(issue);
    }
    print_summary(validator.summary());

    if (options.show_asset_browser) {
        // Phase 11 asset browser. Dumps the cooked chart manifest and
        // its dependency graph as an InspectorPanel; the same panel
        // data drives the eventual in-engine ImGui asset browser.
        //
        // Honor --cooked-root when the user explicitly overrode it so
        // the asset browser inspects the same tree the validator just
        // checked. The default-discovery fallback is reserved for the
        // case where neither flag is supplied.
        reaktio::tools::AssetBrowserSnapshot snapshot{};
        if (options.cooked_root_set) {
            snapshot = reaktio::tools::build_asset_browser_snapshot(
                options.validation.paths.cooked_root / "charts" / "manifest.ini");
        } else {
            snapshot = reaktio::tools::build_asset_browser_snapshot_default();
        }
        const reaktio::tools::InspectorPanel panel =
            reaktio::tools::build_asset_browser_inspector(snapshot);
        std::cout << '\n' << reaktio::tools::format_inspector_panel(panel);
    }

    if (!valid) {
        std::cout << "Content validation failed.\n";
        return 1;
    }

    std::cout << "Content validation passed.\n";
    return 0;
}