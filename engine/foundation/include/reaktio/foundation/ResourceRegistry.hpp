#pragma once

#include "reaktio/foundation/HandleMap.hpp"
#include "reaktio/foundation/StrongId.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::foundation {

enum class ResourceKind : std::uint8_t {
    Texture,
    Mesh,
    Material,
    ShaderProgram,
    Font,
    AudioClip,
    Blob,
    Count,
};

[[nodiscard]] constexpr std::size_t to_index(ResourceKind kind) noexcept {
    return static_cast<std::size_t>(kind);
}

[[nodiscard]] constexpr std::size_t resource_kind_count() noexcept {
    return to_index(ResourceKind::Count);
}

[[nodiscard]] constexpr std::string_view to_string(ResourceKind kind) noexcept {
    switch (kind) {
    case ResourceKind::Texture:
        return "texture";
    case ResourceKind::Mesh:
        return "mesh";
    case ResourceKind::Material:
        return "material";
    case ResourceKind::ShaderProgram:
        return "shader-program";
    case ResourceKind::Font:
        return "font";
    case ResourceKind::AudioClip:
        return "audio-clip";
    case ResourceKind::Blob:
        return "blob";
    case ResourceKind::Count:
        break;
    }

    return "unknown";
}

struct ResourceHandleTag;
using ResourceHandle = StrongId<ResourceHandleTag, std::uint64_t>;

struct ResourceRecord {
    ResourceHandle handle{};
    ResourceKind kind{ResourceKind::Blob};
    std::string authoring_id;
    std::string runtime_label;
};

struct ResourceRegistrySummary {
    std::size_t resource_count{};
    std::uint64_t revision{};
    std::array<std::size_t, resource_kind_count()> counts_by_kind{};
};

using ResourceRecordStorage = HandleMap<ResourceHandleTag, ResourceRecord>;
using BorrowedResourceRecord = ResourceRecordStorage::ConstBorrowed;

class ResourceRegistry {
  public:
    [[nodiscard]] ResourceHandle register_resource(
        ResourceKind kind,
        std::string_view authoring_id,
        std::string_view runtime_label = {});
    bool release_resource(ResourceHandle handle) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool contains(ResourceHandle handle) const noexcept;
    [[nodiscard]] const ResourceRecord* try_get(ResourceHandle handle) const noexcept;
    [[nodiscard]] BorrowedResourceRecord borrow(ResourceHandle handle) const noexcept;
    [[nodiscard]] const ResourceRecord* find(ResourceKind kind, std::string_view authoring_id) const noexcept;
    [[nodiscard]] BorrowedResourceRecord find_borrow(ResourceKind kind, std::string_view authoring_id) const noexcept;
    [[nodiscard]] ResourceHandle resolve(ResourceKind kind, std::string_view authoring_id) const noexcept;
    [[nodiscard]] std::size_t count(ResourceKind kind) const noexcept;
    [[nodiscard]] std::size_t resource_count() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] ResourceRegistrySummary summary() const noexcept;

  private:
    struct LookupKey {
        ResourceKind kind{ResourceKind::Blob};
        std::string authoring_id;

        [[nodiscard]] bool operator==(const LookupKey& other) const noexcept = default;
    };

    struct LookupKeyHash {
        [[nodiscard]] std::size_t operator()(const LookupKey& key) const noexcept;
    };

    [[nodiscard]] static bool is_valid_kind(ResourceKind kind) noexcept;

    std::unordered_map<LookupKey, ResourceHandle, LookupKeyHash> lookup_;
    ResourceRecordStorage records_;
    std::array<std::size_t, resource_kind_count()> kind_counts_{};
    std::uint64_t revision_{};
};

} // namespace reaktio::foundation