#include "reaktio/gameplay/GameModeRegistry.hpp"

namespace reaktio::gameplay {

bool GameModeRegistry::has_mode(std::string_view id) const noexcept {
    for (const RegisteredMode& registered_mode : registered_modes_) {
        if (registered_mode.descriptor.id == id) {
            return true;
        }
    }

    return false;
}

std::span<const RegisteredMode> GameModeRegistry::registered_modes() const noexcept {
    return std::span<const RegisteredMode>{registered_modes_.data(), registered_modes_.size()};
}

std::unique_ptr<IGameMode> GameModeRegistry::create_first() const {
    if (registered_modes_.empty()) {
        return nullptr;
    }

    return registered_modes_.front().create();
}

std::unique_ptr<IGameMode> GameModeRegistry::create_by_id(std::string_view id) const {
    for (const RegisteredMode& registered_mode : registered_modes_) {
        if (registered_mode.descriptor.id == id) {
            return registered_mode.create();
        }
    }

    return nullptr;
}

} // namespace reaktio::gameplay