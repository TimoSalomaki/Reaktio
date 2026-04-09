#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::platform {

struct InputActionBinding {
    std::string action_id;
    std::string primary;
    std::string secondary;
};

class InputBindingsConfig {
  public:
    void clear() noexcept {
        bindings_.clear();
    }

    void set_action_binding(
        std::string_view action_id,
        std::string_view primary,
        std::string_view secondary = {}) {
        for (InputActionBinding& binding : bindings_) {
            if (binding.action_id == action_id) {
                binding.primary = std::string(primary);
                binding.secondary = std::string(secondary);
                return;
            }
        }

        bindings_.push_back(InputActionBinding{
            .action_id = std::string(action_id),
            .primary = std::string(primary),
            .secondary = std::string(secondary),
        });
    }

    [[nodiscard]] const InputActionBinding* find_action(std::string_view action_id) const noexcept {
        for (const InputActionBinding& binding : bindings_) {
            if (binding.action_id == action_id) {
                return &binding;
            }
        }

        return nullptr;
    }

    [[nodiscard]] std::span<const InputActionBinding> actions() const noexcept {
        return std::span<const InputActionBinding>{bindings_.data(), bindings_.size()};
    }

    [[nodiscard]] std::size_t action_count() const noexcept {
        return bindings_.size();
    }

    [[nodiscard]] std::size_t binding_count() const noexcept {
        std::size_t count = 0;
        for (const InputActionBinding& binding : bindings_) {
            if (!binding.primary.empty()) {
                ++count;
            }
            if (!binding.secondary.empty()) {
                ++count;
            }
        }

        return count;
    }

  private:
    std::vector<InputActionBinding> bindings_;
};

} // namespace reaktio::platform