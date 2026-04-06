#include "reaktio/platform/StackProbe.hpp"

#include <SDL3/SDL_version.h>
#include <bgfx/bgfx.h>

#if defined(_WIN32)
#include <windows.h>

#include <psapi.h>
#endif

namespace reaktio::platform {

StackProbe capture_stack_probe() noexcept {
    return StackProbe{
        .compiled_sdl_version = SDL_VERSION,
        .linked_sdl_version = SDL_GetVersion(),
        .sdl_revision = SDL_GetRevision(),
        .bgfx_noop_renderer_name = bgfx::getRendererName(bgfx::RendererType::Noop),
    };
}

std::size_t query_process_resident_memory_mib() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) == 0) {
        return 0;
    }

    return static_cast<std::size_t>(counters.WorkingSetSize / (1024ull * 1024ull));
#else
    return 0;
#endif
}

} // namespace reaktio::platform