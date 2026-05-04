#include "reaktio/content/ContentPipeline.hpp"
#include "reaktio/foundation/CrashSafeLog.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

bool apply_cli_argument(
    int argc,
    char** argv,
    reaktio::content::ContentRootPaths& paths,
    bool& raw_root_set,
    bool& cooked_root_set) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--raw-root") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --raw-root\n";
                return false;
            }
            paths.raw_root = std::filesystem::absolute(std::filesystem::path(argv[++index]));
            raw_root_set = true;
            continue;
        }
        if (argument == "--cooked-root") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --cooked-root\n";
                return false;
            }
            paths.cooked_root = std::filesystem::absolute(std::filesystem::path(argv[++index]));
            cooked_root_set = true;
            continue;
        }
        if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: reaktio_content_cooker [--raw-root <path>] [--cooked-root <path>]\n";
            return false;
        }

        std::cerr << "Unknown argument: " << argument << '\n';
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    reaktio::content::ContentRootPaths paths{};
    bool raw_root_set = false;
    bool cooked_root_set = false;
    if (!apply_cli_argument(argc, argv, paths, raw_root_set, cooked_root_set)) {
        return 1;
    }

    if (!raw_root_set || !cooked_root_set) {
        const std::optional<reaktio::content::ContentRootPaths> default_paths = reaktio::content::find_default_content_roots();
        if (!default_paths) {
            std::cerr << "Unable to locate content/raw and content/cooked roots.\n";
            return 1;
        }
        if (!raw_root_set) {
            paths.raw_root = default_paths->raw_root;
        }
        if (!cooked_root_set) {
            paths.cooked_root = default_paths->cooked_root;
        }
    }

    reaktio::foundation::CrashSafeLog log;
    log.attach_mirror_stream(&std::cout);
    (void)log.open_file(std::filesystem::current_path() / "logs" / "reaktio-content-cooker.log");

    reaktio::content::ContentPipeline pipeline;
    if (!pipeline.cook_all(paths, log)) {
        return 1;
    }

    const reaktio::content::ContentCookSummary& summary = pipeline.summary();
    std::cout << "Cooked charts=" << summary.cooked_chart_count
              << " events=" << summary.total_chart_events
              << " interactive=" << summary.total_interactive_cues
              << " chart-manifest=" << summary.chart_manifest_output_path.string()
              << " render-assets=" << summary.cooked_render_asset_count
              << " render-manifest=" << summary.render_manifest_output_path.string() << '\n';
    return 0;
}