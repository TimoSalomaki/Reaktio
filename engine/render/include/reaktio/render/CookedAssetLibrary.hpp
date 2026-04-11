#pragma once

#include "reaktio/foundation/ResourceRegistry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::render {

enum class CookedTextureFormat : std::uint8_t {
    Rgba8,
};

struct TextureAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path payload_path;
    CookedTextureFormat format{CookedTextureFormat::Rgba8};
    std::uint16_t width{};
    std::uint16_t height{};
    std::vector<std::uint8_t> pixel_bytes;
};

struct MeshAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path payload_path;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::uint16_t> indices;
};

struct FontGlyphRecord {
    char32_t codepoint{};
    float advance{};
    float bearing_x{};
    float bearing_y{};
    std::array<float, 4> uv_rect{};
};

struct FontAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path payload_path;
    float line_height{};
    std::uint16_t atlas_width{};
    std::uint16_t atlas_height{};
    std::vector<FontGlyphRecord> glyphs;
};

struct CookedAssetLibrarySummary {
    bool loaded_from_manifest{};
    std::filesystem::path manifest_path;
    std::size_t texture_count{};
    std::size_t mesh_count{};
    std::size_t font_count{};
    std::size_t total_payload_bytes{};
};

class CookedAssetLibrary {
  public:
    [[nodiscard]] bool load(
        foundation::ResourceRegistry& resource_registry,
        foundation::CrashSafeLog& log);
    void clear() noexcept;

    [[nodiscard]] const TextureAssetRecord* try_get_texture(foundation::ResourceHandle resource) const noexcept;
    [[nodiscard]] const MeshAssetRecord* try_get_mesh(foundation::ResourceHandle resource) const noexcept;
    [[nodiscard]] const FontAssetRecord* try_get_font(foundation::ResourceHandle resource) const noexcept;
    [[nodiscard]] const CookedAssetLibrarySummary& summary() const noexcept;

  private:
    std::unordered_map<std::uint64_t, TextureAssetRecord> textures_;
    std::unordered_map<std::uint64_t, MeshAssetRecord> meshes_;
    std::unordered_map<std::uint64_t, FontAssetRecord> fonts_;
    CookedAssetLibrarySummary summary_{};
};

} // namespace reaktio::render