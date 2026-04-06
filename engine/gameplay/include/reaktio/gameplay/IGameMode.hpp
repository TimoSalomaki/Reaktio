#pragma once

#include <string_view>

namespace reaktio::gameplay {

class IModeHost;

struct ModeDescriptor {
    std::string_view id;
    std::string_view display_name;
    std::string_view description;
};

class IGameMode {
  public:
    virtual ~IGameMode() = default;

    [[nodiscard]] virtual const ModeDescriptor& descriptor() const noexcept = 0;
    virtual void on_enter(IModeHost& host) = 0;
    virtual void on_fixed_step(IModeHost& host, double fixed_delta_seconds) = 0;
    virtual void on_render_extract(IModeHost& host, double interpolation_alpha) = 0;
    virtual void on_exit(IModeHost& host) = 0;
};

} // namespace reaktio::gameplay