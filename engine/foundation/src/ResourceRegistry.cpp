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

    LookupKey key{
        .kind = kind,
        .authoring_id = std::string(authoring_id),
    };
    const std::string resolved_runtime_label = runtime_label.empty()
        ? key.authoring_id
        : std::string(runtime_label);

    if (auto it = lookup_.find(key); it != lookup_.end()) {
        const ResourceHandle replacement = records_.replace(
            it->second,
            ResourceRecord{
                .kind = kind,
                .authoring_id = key.authoring_id,
                .runtime_label = resolved_runtime_label,
            });
        if (replacement.valid()) {
            if (ResourceRecord* record = records_.try_get(replacement)) {
                record->handle = replacement;
            }
            it->second = replacement;
            ++revision_;
            return replacement;
        }

        lookup_.erase(it);
    }

    const ResourceHandle handle = records_.emplace(ResourceRecord{
        .kind = kind,
        .authoring_id = key.authoring_id,
        .runtime_label = resolved_runtime_label,
    });
    if (!handle.valid()) {
        return {};
    }

    if (ResourceRecord* record = records_.try_get(handle)) {
        record->handle = handle;
    }

    lookup_.emplace(std::move(key), handle);
    ++kind_counts_[to_index(kind)];
    ++revision_;
    return handle;
}

bool ResourceRegistry::release_resource(ResourceHandle handle) noexcept {
    const ResourceRecord* record = records_.try_get(handle);
    if (record == nullptr) {
        return false;
    }

    lookup_.erase(LookupKey{.kind = record->kind, .authoring_id = record->authoring_id});
    --kind_counts_[to_index(record->kind)];

    (void)records_.erase(handle);
    ++revision_;
    return true;
}

void ResourceRegistry::clear() noexcept {
    lookup_.clear();
    records_.clear();
    kind_counts_.fill(0);
    revision_ = 0;
}

bool ResourceRegistry::contains(ResourceHandle handle) const noexcept {
    return records_.contains(handle);
}

const ResourceRecord* ResourceRegistry::try_get(ResourceHandle handle) const noexcept {
    return records_.try_get(handle);
}

BorrowedResourceRecord ResourceRegistry::borrow(ResourceHandle handle) const noexcept {
    return records_.borrow(handle);
}

const ResourceRecord* ResourceRegistry::find(ResourceKind kind, std::string_view authoring_id) const noexcept {
    return try_get(resolve(kind, authoring_id));
}

BorrowedResourceRecord ResourceRegistry::find_borrow(ResourceKind kind, std::string_view authoring_id) const noexcept {
    return borrow(resolve(kind, authoring_id));
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
    return records_.size();
}

std::uint64_t ResourceRegistry::revision() const noexcept {
    return revision_;
}

ResourceRegistrySummary ResourceRegistry::summary() const noexcept {
    return ResourceRegistrySummary{
        .resource_count = records_.size(),
        .revision = revision_,
        .counts_by_kind = kind_counts_,
    };
}

} // namespace reaktio::foundation