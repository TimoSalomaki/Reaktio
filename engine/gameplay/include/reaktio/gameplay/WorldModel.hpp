#pragma once

#include "reaktio/foundation/StrongId.hpp"

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace reaktio::gameplay {

struct WorldEntityTag;
using WorldEntity = foundation::StrongId<WorldEntityTag, std::uint32_t>;

struct EntityName {
    std::string value;
};

class WorldModel {
  public:
    WorldModel() = default;

    [[nodiscard]] WorldEntity create_entity(std::string_view name = {});
    void destroy_entity(WorldEntity entity) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool contains(WorldEntity entity) const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept;

    template <typename Component, typename... Args>
    Component& emplace(WorldEntity entity, Args&&... args) {
        return registry_.emplace<Component>(to_entt(entity), std::forward<Args>(args)...);
    }

    template <typename Component, typename... Args>
    Component& get_or_emplace(WorldEntity entity, Args&&... args) {
        return registry_.get_or_emplace<Component>(to_entt(entity), std::forward<Args>(args)...);
    }

    template <typename Component>
    [[nodiscard]] bool has(WorldEntity entity) const noexcept {
        return registry_.all_of<Component>(to_entt(entity));
    }

    template <typename Component>
    [[nodiscard]] Component& get(WorldEntity entity) {
        return registry_.get<Component>(to_entt(entity));
    }

    template <typename Component>
    [[nodiscard]] const Component& get(WorldEntity entity) const {
        return registry_.get<Component>(to_entt(entity));
    }

    template <typename Component>
    [[nodiscard]] Component* try_get(WorldEntity entity) noexcept {
        return registry_.template try_get<Component>(to_entt(entity));
    }

    template <typename Component>
    [[nodiscard]] const Component* try_get(WorldEntity entity) const noexcept {
        return registry_.template try_get<Component>(to_entt(entity));
    }

    template <typename Component>
    void remove(WorldEntity entity) {
        registry_.remove<Component>(to_entt(entity));
    }

    template <typename... Components, typename Func>
    void for_each(Func&& func) {
        auto view = registry_.view<Components...>();
        for (const entt::entity entity : view) {
            if constexpr (sizeof...(Components) == 1u) {
                func(to_world_entity(entity), view.template get<Components...>(entity));
            } else {
                std::apply(
                    [&](Components&... components) {
                        func(to_world_entity(entity), components...);
                    },
                    view.template get<Components...>(entity));
            }
        }
    }

    template <typename... Components, typename Func>
    void for_each(Func&& func) const {
        auto view = registry_.view<const Components...>();
        for (const entt::entity entity : view) {
            if constexpr (sizeof...(Components) == 1u) {
                func(to_world_entity(entity), view.template get<const Components...>(entity));
            } else {
                std::apply(
                    [&](const Components&... components) {
                        func(to_world_entity(entity), components...);
                    },
                    view.template get<const Components...>(entity));
            }
        }
    }

  private:
    static constexpr entt::entity to_entt(WorldEntity entity) noexcept {
        return entity.valid() ? static_cast<entt::entity>(entity.value() - 1u) : entt::null;
    }

    static constexpr WorldEntity to_world_entity(entt::entity entity) noexcept {
        return WorldEntity{static_cast<std::uint32_t>(entt::to_integral(entity)) + 1u};
    }

    entt::registry registry_;
};

} // namespace reaktio::gameplay