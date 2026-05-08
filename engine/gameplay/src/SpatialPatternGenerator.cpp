#include "reaktio/gameplay/SpatialPatternGenerator.hpp"

#include "reaktio/foundation/DeterministicRandom.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

namespace {

constexpr float k_two_pi = 6.28318530717958647692f;

[[nodiscard]] inline float saturate(float v) noexcept { return std::min(std::max(v, 0.0f), 1.0f); }

[[nodiscard]] RingSliceHazard make_base_hazard(const PatternRequest& request) noexcept {
    RingSliceHazard hazard{};
    hazard.spawn_radius = request.spawn_radius;
    hazard.radial_velocity_per_second = request.radial_velocity_per_second;
    hazard.half_arc_radians = std::max(0.001f, request.slice_arc_radians * 0.5f);
    hazard.band_offset = 0.0f;
    hazard.band_half_height = 0.5f;
    return hazard;
}

void emit_wall_with_gap(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards) {
    const std::uint32_t slice_count =
        request.slice_count > 0u ? request.slice_count : 1u;
    const std::uint32_t gap_count = std::min(request.gap_count, slice_count);
    const std::uint32_t solid_count = slice_count - gap_count;
    if (solid_count == 0u) {
        return;
    }
    // Place gaps uniformly. Rotate the gap pattern by a seeded jitter
    // (in [0, slice_count)) so successive walls don't always have the
    // same gap orientation.
    const std::uint32_t gap_rotation = gap_count > 0u
        ? rng.next_u32(0u, slice_count - 1u)
        : 0u;
    const float arc_per_slot = k_two_pi / static_cast<float>(slice_count);

    std::vector<std::uint8_t> is_gap(slice_count, static_cast<std::uint8_t>(0));
    if (gap_count > 0u) {
        for (std::uint32_t g = 0; g < gap_count; ++g) {
            // Spread the gaps roughly evenly around the ring, then rotate.
            const std::uint32_t target = (g * (slice_count / gap_count) + gap_rotation) % slice_count;
            is_gap[target] = static_cast<std::uint8_t>(1);
        }
    }

    for (std::uint32_t i = 0; i < slice_count; ++i) {
        if (is_gap[i] != 0u) {
            continue;
        }
        RingSliceHazard hazard = make_base_hazard(request);
        hazard.heading_radians = static_cast<float>(i) * arc_per_slot;
        hazard.hazard_id =
            request.hazard_id_offset + static_cast<std::uint64_t>(out_hazards.size() + 1u);
        out_hazards.push_back(hazard);
    }
}

void emit_alternating_halves(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards) {
    // Two opposing crescents, each spanning ~half the ring with a small
    // gap between them. pattern_parameter biases the crescent split: 0
    // means perfectly opposing halves; positive values widen the first
    // crescent and narrow the second by the same amount, useful when
    // chaining requests to telegraph a gap drift.
    const float gap_arc = std::max(0.05f, request.slice_arc_radians);
    const float total_solid = std::max(0.10f, k_two_pi - gap_arc * 2.0f);
    const float bias = request.pattern_parameter;
    const float arc_a = std::clamp(total_solid * 0.5f + bias, 0.05f, total_solid - 0.05f);
    const float arc_b = total_solid - arc_a;
    const float jitter = (rng.next_unit_f32() - 0.5f) * 0.5f;  // ~[-0.25, +0.25] rad orientation jitter.
    const float center_a = jitter;
    const float center_b = jitter + 3.14159265358979323846f;

    RingSliceHazard hazard = make_base_hazard(request);
    hazard.half_arc_radians = arc_a * 0.5f;
    hazard.heading_radians = center_a;
    hazard.hazard_id =
        request.hazard_id_offset + static_cast<std::uint64_t>(out_hazards.size() + 1u);
    out_hazards.push_back(hazard);

    hazard.half_arc_radians = arc_b * 0.5f;
    hazard.heading_radians = center_b;
    hazard.hazard_id =
        request.hazard_id_offset + static_cast<std::uint64_t>(out_hazards.size() + 1u);
    out_hazards.push_back(hazard);
}

void emit_spiral(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards) {
    (void)rng;
    const std::uint32_t slice_count = std::max(1u, request.slice_count);
    const float heading_step = request.pattern_parameter != 0.0f
        ? request.pattern_parameter
        : (k_two_pi / static_cast<float>(slice_count));
    for (std::uint32_t i = 0; i < slice_count; ++i) {
        RingSliceHazard hazard = make_base_hazard(request);
        hazard.heading_radians = static_cast<float>(i) * heading_step;
        // Stagger spawn radius so the spiral arrives over time rather than
        // simultaneously, keeping the pattern readable.
        hazard.spawn_radius = request.spawn_radius +
            static_cast<float>(i) * std::abs(request.radial_velocity_per_second) * 0.20f;
        hazard.hazard_id =
            request.hazard_id_offset + static_cast<std::uint64_t>(out_hazards.size() + 1u);
        out_hazards.push_back(hazard);
    }
}

void emit_random_scatter(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards) {
    const std::uint32_t slice_count = std::max(1u, request.slice_count);
    const float jitter_ratio = saturate(request.pattern_parameter);
    for (std::uint32_t i = 0; i < slice_count; ++i) {
        RingSliceHazard hazard = make_base_hazard(request);
        const float base = static_cast<float>(i) * (k_two_pi / static_cast<float>(slice_count));
        const float jitter = (rng.next_unit_f32() - 0.5f) * jitter_ratio * k_two_pi;
        hazard.heading_radians = base + jitter;
        hazard.hazard_id =
            request.hazard_id_offset + static_cast<std::uint64_t>(out_hazards.size() + 1u);
        out_hazards.push_back(hazard);
    }
}

} // namespace

std::string_view to_string(PatternKind kind) noexcept {
    switch (kind) {
    case PatternKind::WallWithGap:
        return "wall-with-gap";
    case PatternKind::AlternatingHalves:
        return "alternating-halves";
    case PatternKind::Spiral:
        return "spiral";
    case PatternKind::RandomScatter:
        return "random-scatter";
    }
    return "unknown";
}

std::size_t generate_ring_slice_pattern(
    foundation::DeterministicRng& rng,
    const PatternRequest& request,
    std::vector<RingSliceHazard>& out_hazards) {
    const std::size_t before = out_hazards.size();
    switch (request.kind) {
    case PatternKind::WallWithGap:
        emit_wall_with_gap(rng, request, out_hazards);
        break;
    case PatternKind::AlternatingHalves:
        emit_alternating_halves(rng, request, out_hazards);
        break;
    case PatternKind::Spiral:
        emit_spiral(rng, request, out_hazards);
        break;
    case PatternKind::RandomScatter:
        emit_random_scatter(rng, request, out_hazards);
        break;
    }
    return out_hazards.size() - before;
}

RingSliceRuntime make_ring_slice_runtime(const RingSliceHazard& hazard) noexcept {
    RingSliceRuntime runtime{};
    runtime.sweep.radius = hazard.spawn_radius;
    runtime.sweep.radial_velocity_per_second = hazard.radial_velocity_per_second;
    runtime.sweep.heading_radians = hazard.heading_radians;
    runtime.arc_half_extent_radians = std::max(0.001f, hazard.half_arc_radians);
    runtime.band_half_height = std::max(0.05f, hazard.band_half_height);
    runtime.hazard_id = hazard.hazard_id;
    return runtime;
}

} // namespace reaktio::gameplay
