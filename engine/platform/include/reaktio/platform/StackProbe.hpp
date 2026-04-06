#pragma once

#include <string_view>

namespace reaktio::platform {

struct StackProbe {
    int compiled_sdl_version{};
    int linked_sdl_version{};
    std::string_view sdl_revision;
    std::string_view bgfx_noop_renderer_name;
};

[[nodiscard]] StackProbe capture_stack_probe() noexcept;
[[nodiscard]] std::size_t query_process_resident_memory_mib() noexcept;

} // namespace reaktio::platform