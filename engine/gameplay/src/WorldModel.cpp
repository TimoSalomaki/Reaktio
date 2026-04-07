#include "reaktio/gameplay/WorldModel.hpp"

namespace reaktio::gameplay {

WorldEntity WorldModel::create_entity(std::string_view name) {
    const entt::entity entity = registry_.create();
    try {
        if (!name.empty()) {
            registry_.emplace<EntityName>(entity, std::string(name));
        }
    } catch (...) {
        registry_.destroy(entity);
        throw;
    }

    ++live_entity_count_;

    return to_world_entity(entity);
}

void WorldModel::destroy_entity(WorldEntity entity) noexcept {
    const entt::entity registry_entity = to_entt(entity);
    if (registry_entity != entt::null && registry_.valid(registry_entity)) {
        registry_.destroy(registry_entity);
        if (live_entity_count_ > 0) {
            --live_entity_count_;
        }
    }
}

void WorldModel::reset() noexcept {
    registry_.clear();
    live_entity_count_ = 0;
}

bool WorldModel::contains(WorldEntity entity) const noexcept {
    const entt::entity registry_entity = to_entt(entity);
    return registry_entity != entt::null && registry_.valid(registry_entity);
}

std::size_t WorldModel::entity_count() const noexcept {
    return live_entity_count_;
}

} // namespace reaktio::gameplay