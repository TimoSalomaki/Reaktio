#pragma once

#include <cstdint>
#include <string_view>

namespace reaktio::gameplay {

class IModeHost;

inline constexpr std::uint32_t k_current_mode_api_version = 1;

using ModeCapabilityFlags = std::uint64_t;

namespace ModeCapabilities {
inline constexpr ModeCapabilityFlags None = 0;
inline constexpr ModeCapabilityFlags UsesActionInput = 1ull << 0u;
inline constexpr ModeCapabilityFlags UsesTextInput = 1ull << 1u;
inline constexpr ModeCapabilityFlags UsesAnalogInput = 1ull << 2u;
inline constexpr ModeCapabilityFlags UsesTransport = 1ull << 3u;
inline constexpr ModeCapabilityFlags UsesWorldModel = 1ull << 4u;
inline constexpr ModeCapabilityFlags EmitsRenderPackets = 1ull << 5u;
inline constexpr ModeCapabilityFlags RecordsReplay = 1ull << 6u;
inline constexpr ModeCapabilityFlags SupportsPractice = 1ull << 7u;
inline constexpr ModeCapabilityFlags SupportsCalibration = 1ull << 8u;
} // namespace ModeCapabilities

[[nodiscard]] constexpr bool has_mode_capability(
    ModeCapabilityFlags capabilities,
    ModeCapabilityFlags capability) noexcept {
    return (capabilities & capability) == capability;
}

enum class ModeLifecycleReason : std::uint8_t {
    Startup,
    Shutdown,
    Restart,
    ModeSwitch,
    HotReload,
};

[[nodiscard]] constexpr std::string_view to_string(ModeLifecycleReason reason) noexcept {
    switch (reason) {
    case ModeLifecycleReason::Startup:
        return "startup";
    case ModeLifecycleReason::Shutdown:
        return "shutdown";
    case ModeLifecycleReason::Restart:
        return "restart";
    case ModeLifecycleReason::ModeSwitch:
        return "mode-switch";
    case ModeLifecycleReason::HotReload:
        return "hot-reload";
    }

    return "unknown";
}

struct ModeDescriptor {
    std::string_view id;
    std::string_view display_name;
    std::string_view description;
    std::string_view family;
    std::uint32_t api_version{k_current_mode_api_version};
    ModeCapabilityFlags capabilities{ModeCapabilities::None};
};

struct ModeEnterContext {
    ModeLifecycleReason reason{ModeLifecycleReason::Startup};
    std::uint64_t frame_index{};
    std::uint64_t fixed_step_index{};
};

struct ModeFrameContext {
    std::uint64_t frame_index{};
    std::uint64_t fixed_step_index{};
    double frame_delta_seconds{};
    double interpolation_alpha{};
};

struct ModeFixedStepContext {
    std::uint64_t frame_index{};
    std::uint64_t fixed_step_index{};
    double fixed_delta_seconds{};
};

struct ModeRenderContext {
    std::uint64_t frame_index{};
    std::uint64_t fixed_step_index{};
    double interpolation_alpha{};
};

struct ModeExitContext {
    ModeLifecycleReason reason{ModeLifecycleReason::Shutdown};
    std::uint64_t frame_index{};
    std::uint64_t fixed_step_index{};
};

class IGameMode {
  public:
    virtual ~IGameMode() = default;

    [[nodiscard]] virtual const ModeDescriptor& descriptor() const noexcept = 0;
    virtual void on_enter(IModeHost& host, const ModeEnterContext& context) = 0;
    virtual void on_frame_begin(IModeHost& host, const ModeFrameContext& context);
    virtual void on_fixed_step(IModeHost& host, const ModeFixedStepContext& context) = 0;
    virtual void on_render_extract(IModeHost& host, const ModeRenderContext& context) = 0;
    virtual void on_exit(IModeHost& host, const ModeExitContext& context) = 0;
};

} // namespace reaktio::gameplay