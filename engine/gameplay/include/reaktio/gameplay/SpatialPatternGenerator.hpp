#pragma once

#include "reaktio/gameplay/SpatialKinematics.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::foundation {
class DeterministicRng;
} // namespace reaktio::foundation

namespace reaktio::gameplay {

// Engine-layer seeded pattern generator. Emits ring-slice hazards (the
// natural unit for radial obstacle modes) deterministically from a seed
// and a request descriptor. Replay-safe: identical (seed, request) input
// always yields identical output, regardless of the run's wall-clock,
// frame index, or thread order.
//
// Patterns are an open set: each kind is a recipe parameterized by the
// request's slice_count, gap_count, etc. Modes that need additional
// archetypes can compose multiple PatternRequests, or layer their own
// generator alongside this one.

struct RingSliceHazard {
    // Heading angle along the orbit plane (radians). The hazard occupies
    // [heading_radians - half_arc, heading_radians + half_arc].
    float heading_radians{0.0f};
    float half_arc_radians{0.10f};
    // Initial radius at spawn, plus the radial speed that drives the
    // hazard inward. See SpatialKinematics::RadialSweepState; modes
    // typically convert RingSliceHazard into a RadialSweepState +
    // OrientedBoxVolume pair when activating the hazard.
    float spawn_radius{20.0f};
    float radial_velocity_per_second{-6.0f};
    // Optional vertical band (units of orbit-plane-up vector) so modes
    // can fan the same pattern across multiple altitudes. Default 0
    // means the hazard sits in the orbit plane.
    float band_offset{0.0f};
    float band_half_height{0.5f};
    // Stable identifier so modes can correlate generated hazards back to
    // the originating PatternRequest in scoring/replay. Generator
    // populates this; callers should not mutate it.
    std::uint64_t hazard_id{};
};

enum class PatternKind : std::uint8_t {
    WallWithGap = 0,        // One full ring with `gap_count` evenly-spaced gaps.
    AlternatingHalves = 1,  // Two opposing half-rings with the gap rotating per beat.
    Spiral = 2,             // Slices placed along a rotating heading.
    RandomScatter = 3,      // Uniform scatter, replay-safe via the seeded RNG.
};

[[nodiscard]] std::string_view to_string(PatternKind kind) noexcept;

struct PatternRequest {
    PatternKind kind{PatternKind::WallWithGap};
    std::uint32_t slice_count{16};
    std::uint32_t gap_count{1};
    float spawn_radius{20.0f};
    float radial_velocity_per_second{-6.0f};
    float slice_arc_radians{0.30f};
    // Pattern-specific knob:
    //   - WallWithGap: ignored (gap layout is driven by slice_count and
    //     gap_count alone).
    //   - AlternatingHalves: extra crescent imbalance in radians. Value 0
    //     gives two equal half-rings; positive values lengthen one half
    //     and shorten the other.
    //   - Spiral: heading delta per slice in radians. Value 0 falls back
    //     to (2*pi / slice_count).
    //   - RandomScatter: heading jitter ratio in [0, 1].
    float pattern_parameter{0.0f};
    // Per-pattern offset rolled into hazard_id so multiple PatternRequests
    // sharing one stream still produce unique hazard IDs.
    std::uint64_t hazard_id_offset{0};
};

// Generate hazards into the supplied buffer. The buffer is appended to
// (not cleared) so the same caller can chain multiple patterns into one
// hazard list. Returns the number of hazards appended.
std::size_t generate_ring_slice_pattern(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards);

// Convert a finalized hazard into the runtime motion + collision pair
// modes typically run with. The returned RadialSweepState carries
// the hazard's heading and current radius; the heading_radians is fixed
// for ring-slice patterns. The runtime stores the hazard's ANGULAR arc
// half-extent (radians) and band half-height (world units); modes turn
// the angular arc into a world-space tangent extent at the hazard's
// CURRENT radius each fixed step (extent = radius * angular_extent),
// so the collision box scales with the hazard as it sweeps inward
// instead of "growing" angularly.
struct RingSliceRuntime {
    RadialSweepState sweep{};
    float arc_half_extent_radians{};
    float band_half_height{};
    std::uint64_t hazard_id{};
};

[[nodiscard]] RingSliceRuntime make_ring_slice_runtime(
    const RingSliceHazard& hazard) noexcept;

} // namespace reaktio::gameplay
