#include "reaktio/gameplay/WorldModel.hpp"

namespace reaktio::gameplay {

WorldEntity WorldModel::create_entity(std::string_view name) {
    const entt::entity entity = registry_.create();
    if (!name.empty()) {
        registry_.emplace<EntityName>(entity, std::string(name));
    }

    return to_world_entity(entity);
}

void WorldModel::destroy_entity(WorldEntity entity) noexcept {
    const entt::entity registry_entity = to_entt(entity);
    if (registry_entity != entt::null && registry_.valid(registry_entity)) {
        registry_.destroy(registry_entity);
    }
}

void WorldModel::reset() noexcept {
    registry_.clear();
}

bool WorldModel::contains(WorldEntity entity) const noexcept {
    const entt::entity registry_entity = to_entt(entity);
    return registry_entity != entt::null && registry_.valid(registry_entity);
}

std::size_t WorldModel::entity_count() const noexcept {
    std::size_t live_entities = 0;
    if (const auto* entities = registry_.storage<entt::entity>(); entities != nullptr) {
        for ([[maybe_unused]] const auto [entity] : entities->each()) {
            ++live_entities;
        }
    }

    return live_entities;
}

} // namespace reaktio::gameplay