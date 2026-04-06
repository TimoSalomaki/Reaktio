#pragma once

#include "reaktio/gameplay/IGameMode.hpp"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

using ModeFactory = std::unique_ptr<IGameMode> (*)();

struct RegisteredMode {
    ModeDescriptor descriptor;
    ModeFactory create;
};

class GameModeRegistry {
  public:
    template <typename ModeType>
    [[nodiscard]] bool register_mode();

    [[nodiscard]] bool has_mode(std::string_view id) const noexcept;
    [[nodiscard]] std::span<const RegisteredMode> registered_modes() const noexcept;
    [[nodiscard]] std::unique_ptr<IGameMode> create_first() const;
    [[nodiscard]] std::unique_ptr<IGameMode> create_by_id(std::string_view id) const;

  private:
    std::vector<RegisteredMode> registered_modes_;
};

template <typename ModeType>
bool GameModeRegistry::register_mode() {
    const ModeDescriptor& descriptor = ModeType::mode_descriptor();

  if (has_mode(descriptor.id)) {
    return false;
  }

    registered_modes_.push_back(RegisteredMode{
        .descriptor = descriptor,
        .create = []() -> std::unique_ptr<IGameMode> { return std::make_unique<ModeType>(); },
    });

  return true;
}

} // namespace reaktio::gameplay