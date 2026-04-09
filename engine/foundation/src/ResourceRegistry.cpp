#include "reaktio/foundation/ResourceRegistry.hpp"

#include <functional>
#include <limits>

namespace reaktio::foundation {

namespace {

constexpr std::uint64_t k_slot_mask = 0xffffffffull;

[[nodiscard]] std::size_t hash_combine(std::size_t lhs, std::size_t rhs) noexcept {
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ull + (lhs << 6u) + (lhs >> 2u));
}

} // namespace

std::size_t ResourceRegistry::LookupKeyHash::operator()(const LookupKey& key) const noexcept {
    return hash_combine(
        std::hash<std::string_view>{}(key.authoring_id),
        std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.kind)));
}

bool ResourceRegistry::is_valid_kind(ResourceKind kind) noexcept {
    const std::size_t index = to_index(kind);
    return kind != ResourceKind::Count && index < resource_kind_count();
}

ResourceHandle ResourceRegistry::register_resource(
    ResourceKind kind,
    std::string_view authoring_id,
    std::string_view runtime_label) {
    if (!is_valid_kind(kind) || authoring_id.empty()) {
        return {};
    }

    if (auto it = lookup_.find(LookupKey{.kind = kind, .authoring_id = std::string(authoring_id)}); it != lookup_.end()) {
        const std::uint32_t index = slot_index(it->second);
        if (index < slots_.size()) {
            ResourceSlot& slot = slots_[index];
            if (slot.occupied && slot.generation == generation(it->second)) {
                slot.generation = next_generation(slot.generation);
                const ResourceHandle handle = make_handle(index, slot.generation);
                slot.record = ResourceRecord{
                    .handle = handle,
                    .kind = kind,
                    .authoring_id = std::string(authoring_id),
                    .runtime_label = runtime_label.empty() ? std::string(authoring_id) : std::string(runtime_label),
                };
                it->second = handle;
                ++revision_;
                return handle;
            }
        }

        lookup_.erase(it);
    }

    std::uint32_t index{};
    if (free_indices_.empty()) {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(ResourceSlot{});
    } else {
        index = free_indices_.back();
        free_indices_.pop_back();
    }

    ResourceSlot& slot = slots_[index];
    if (slot.generation == 0u) {
        slot.generation = 1u;
    }

    slot.occupied = true;
    slot.record = ResourceRecord{
        .handle = make_handle(index, slot.generation),
        .kind = kind,
        .authoring_id = std::string(authoring_id),
        .runtime_label = runtime_label.empty() ? std::string(authoring_id) : std::string(runtime_label),
    };

    lookup_.emplace(LookupKey{.kind = kind, .authoring_id = slot.record.authoring_id}, slot.record.handle);
    ++kind_counts_[to_index(kind)];
    ++resource_count_;
    ++revision_;
    return slot.record.handle;
}

bool ResourceRegistry::release_resource(ResourceHandle handle) noexcept {
    if (!handle.valid()) {
        return false;
    }

    const std::uint32_t index = slot_index(handle);
    if (index >= slots_.size()) {
        return false;
    }

    ResourceSlot& slot = slots_[index];
    if (!slot.occupied || slot.generation != generation(handle) || slot.record.handle != handle) {
        return false;
    }

    lookup_.erase(LookupKey{.kind = slot.record.kind, .authoring_id = slot.record.authoring_id});
    --kind_counts_[to_index(slot.record.kind)];
    --resource_count_;

    slot.occupied = false;
    slot.record = ResourceRecord{};
    slot.generation = next_generation(slot.generation);
    free_indices_.push_back(index);
    ++revision_;
    return true;
}

void ResourceRegistry::clear() noexcept {
    lookup_.clear();
    slots_.clear();
    free_indices_.clear();
    kind_counts_.fill(0);
    resource_count_ = 0;
    revision_ = 0;
}

bool ResourceRegistry::contains(ResourceHandle handle) const noexcept {
    return try_get(handle) != nullptr;
}

const ResourceRecord* ResourceRegistry::try_get(ResourceHandle handle) const noexcept {
    if (!handle.valid()) {
        return nullptr;
    }

    const std::uint32_t index = slot_index(handle);
    if (index >= slots_.size()) {
        return nullptr;
    }

    const ResourceSlot& slot = slots_[index];
    if (!slot.occupied || slot.generation != generation(handle) || slot.record.handle != handle) {
        return nullptr;
    }

    return &slot.record;
}

const ResourceRecord* ResourceRegistry::find(ResourceKind kind, std::string_view authoring_id) const noexcept {
    return try_get(resolve(kind, authoring_id));
}

ResourceHandle ResourceRegistry::resolve(ResourceKind kind, std::string_view authoring_id) const noexcept {
    if (!is_valid_kind(kind) || authoring_id.empty()) {
        return {};
    }

    const auto it = lookup_.find(LookupKey{.kind = kind, .authoring_id = std::string(authoring_id)});
    if (it == lookup_.end()) {
        return {};
    }

    return try_get(it->second) != nullptr ? it->second : ResourceHandle{};
}

std::size_t ResourceRegistry::count(ResourceKind kind) const noexcept {
    return is_valid_kind(kind) ? kind_counts_[to_index(kind)] : 0u;
}

std::size_t ResourceRegistry::resource_count() const noexcept {
    return resource_count_;
}

std::uint64_t ResourceRegistry::revision() const noexcept {
    return revision_;
}

ResourceRegistrySummary ResourceRegistry::summary() const noexcept {
    return ResourceRegistrySummary{
        .resource_count = resource_count_,
        .revision = revision_,
        .counts_by_kind = kind_counts_,
    };
}

ResourceHandle ResourceRegistry::make_handle(std::uint32_t slot_index, std::uint32_t generation) noexcept {
    return ResourceHandle{(static_cast<std::uint64_t>(generation) << 32u) | (static_cast<std::uint64_t>(slot_index) + 1u)};
}

std::uint32_t ResourceRegistry::slot_index(ResourceHandle handle) noexcept {
    return handle.valid()
        ? static_cast<std::uint32_t>((handle.value() & k_slot_mask) - 1u)
        : std::numeric_limits<std::uint32_t>::max();
}

std::uint32_t ResourceRegistry::generation(ResourceHandle handle) noexcept {
    return static_cast<std::uint32_t>(handle.value() >> 32u);
}

std::uint32_t ResourceRegistry::next_generation(std::uint32_t generation) noexcept {
    return generation == std::numeric_limits<std::uint32_t>::max() ? 1u : generation + 1u;
}

} // namespace reaktio::foundation