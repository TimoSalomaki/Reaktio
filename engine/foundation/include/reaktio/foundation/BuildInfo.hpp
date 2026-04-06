#pragma once

#include <string_view>

namespace reaktio::foundation {

struct BuildInfo {
    std::string_view project_name;
    std::string_view project_version;
    std::string_view build_configuration;
};

[[nodiscard]] BuildInfo query_build_info() noexcept;

} // namespace reaktio::foundation