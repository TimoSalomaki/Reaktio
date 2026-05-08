#pragma once

#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

// Engine-layer music-reactive presentation envelopes. Pure functions that
// turn (transport position, tempo map, envelope config) into a normalized
// [0, 1] activation level. Modes pull whatever envelopes they need and
// forward the resulting scalar to render extraction, screen effects,
// haptics, or geometry animation. Replay-safe by construction: the
// envelope value is purely a function of the deterministic transport
// position and the immutable tempo map.
//
// Two envelope archetypes cover the bulk of beat-driven presentation:
//   - PulseEnvelope: an attack/decay shot that fires once per beat or
//     bar boundary. Used for screen flashes, color pulses, geometry
//     "punch" scaling.
//   - SquareWaveEnvelope: alternates 0 / 1 on beat boundaries. Used for
//     stroboscopic effects or beat-quantized animation phases.
//
// Modes that need other archetypes (sawtooth, triangle, exponential
// decay) can layer them on top of the same (position, tempo map) inputs
// without changing this header. A closed enum is intentionally avoided.

enum class PulseEnvelopeUnit : std::uint8_t {
    Beat = 0,
    Bar = 1,
};

[[nodiscard]] std::string_view to_string(PulseEnvelopeUnit unit) noexcept;

struct PulseEnvelopeConfig {
    PulseEnvelopeUnit unit{PulseEnvelopeUnit::Beat};
    // Stride in beats (or bars depending on unit). 1 means every beat,
    // 2 means every other beat, 4 means every bar in 4/4, etc. Float so
    // half-bar pulses or 3-against-2 polyrhythms are expressible.
    double stride{1.0};
    // Attack/decay are seconds. Total envelope duration is attack + decay;
    // outside that window the envelope returns 0.
    double attack_seconds{0.05};
    double decay_seconds{0.30};
    // Optional offset to shift the pulse boundary forward or backward in
    // beats. Useful for syncing to upbeats / off-beats.
    double phase_offset_beats{0.0};
};

struct SquareWaveEnvelopeConfig {
    PulseEnvelopeUnit unit{PulseEnvelopeUnit::Beat};
    double stride{1.0};
    double phase_offset_beats{0.0};
};

[[nodiscard]] double evaluate_pulse_envelope(
    const PulseEnvelopeConfig& config,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept;

[[nodiscard]] double evaluate_square_wave_envelope(
    const SquareWaveEnvelopeConfig& config,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept;

// Bulk evaluation: modes typically run several envelopes per frame
// (color pulse, screen shake, geometry beat-scale). Bulk helpers fetch
// the rhythm position once, then evaluate each envelope without a
// repeated tempo-map lookup.
struct EnvelopeSample {
    double value{};
    std::string_view label;  // Caller-owned label; the helper just copies the view through.
};

void evaluate_pulse_envelope_bulk(
    std::span<const PulseEnvelopeConfig> configs,
    std::span<const std::string_view> labels,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds,
    std::vector<EnvelopeSample>& out_samples);

} // namespace reaktio::gameplay
