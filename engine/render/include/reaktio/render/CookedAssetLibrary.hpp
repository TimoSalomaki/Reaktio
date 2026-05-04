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
    Bc1,
    Bc3,
    Bc5,
};

enum class CookedTextureStorage : std::uint8_t {
    InlineRgba8,
    Dds,
    Ktx,
};

enum class CookedMeshStorage : std::uint8_t {
    InlineLists,
    BgfxGeometry,
};

enum class CookedFontAtlasStorage : std::uint8_t {
    None,
    RawR8,
};

struct TextureAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path metadata_path;
    std::filesystem::path data_path;
    CookedTextureStorage storage{CookedTextureStorage::InlineRgba8};
    CookedTextureFormat format{CookedTextureFormat::Rgba8};
    std::uint16_t width{};
    std::uint16_t height{};
    bool srgb{true};
    bool generate_mips{};
    std::vector<std::uint8_t> payload_bytes;
};

struct MeshAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path metadata_path;
    std::filesystem::path data_path;
    CookedMeshStorage storage{CookedMeshStorage::InlineLists};
    std::string source_format;
    float scale{1.0f};
    bool compressed{};
    bool flip_v{};
    bool ccw{};
    std::uint8_t pack_normals{};
    std::uint8_t pack_uv{};
    bool generate_tangents{};
    bool barycentric{};
    std::string coordinate_system;
    std::vector<std::uint8_t> payload_bytes;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::uint16_t> indices;
};

struct FontGlyphRecord {
    char32_t codepoint{};
    float advance{};
    float bearing_x{};
    float bearing_y{};
    float width{};
    float height{};
    std::array<float, 4> uv_rect{};
};

struct FontAssetRecord {
    foundation::ResourceHandle resource{};
    std::string authoring_id;
    std::string runtime_label;
    std::filesystem::path metadata_path;
    std::filesystem::path atlas_path;
    CookedFontAtlasStorage atlas_storage{CookedFontAtlasStorage::None};
    float pixel_height{};
    float line_height{};
    std::uint16_t atlas_width{};
    std::uint16_t atlas_height{};
    float line_spacing{1.0f};
    float ascent{};
    float descent{};
    float line_gap{};
    bool sdf{};
    std::vector<std::string> fallback_ids;
    std::vector<std::uint8_t> atlas_bytes;
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
        [[nodiscard]] bool load(
                const std::filesystem::path& manifest_path,
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