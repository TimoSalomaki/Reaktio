#include "reaktio/foundation/BuildInfo.hpp"

namespace reaktio::foundation {

namespace {

constexpr std::string_view k_project_name = REAKTIO_PROJECT_NAME;
constexpr std::string_view k_project_version = REAKTIO_PROJECT_VERSION;
constexpr std::string_view k_build_configuration = REAKTIO_BUILD_CONFIGURATION;

} // namespace

BuildInfo query_build_info() noexcept {
    return BuildInfo{
        .project_name = k_project_name,
        .project_version = k_project_version,
        .build_configuration = k_build_configuration,
    };
}

} // namespace reaktio::foundation