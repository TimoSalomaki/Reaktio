#include "reaktio/gameplay/MusicReactivePresentation.hpp"

#include <algorithm>
#include <cmath>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] double beats_position_for(
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    if (!tempo_map.valid()) {
        return 0.0;
    }
    const rhythm::RhythmPosition pos = tempo_map.position_from_microseconds(position_microseconds);
    if (pos.beat.ticks_per_beat <= 0) {
        return 0.0;
    }
    return static_cast<double>(pos.beat.whole_beats) +
           static_cast<double>(pos.beat.tick_offset_in_beat) /
               static_cast<double>(pos.beat.ticks_per_beat);
}

[[nodiscard]] double bars_position_for(
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    if (!tempo_map.valid()) {
        return 0.0;
    }
    const rhythm::RhythmPosition pos = tempo_map.position_from_microseconds(position_microseconds);
    if (pos.bar.beats_per_bar <= 0 || pos.bar.ticks_per_beat <= 0) {
        return 0.0;
    }
    const double beat_in_bar = static_cast<double>(pos.bar.beat_index_in_bar) +
        static_cast<double>(pos.bar.tick_offset_in_beat) /
            static_cast<double>(pos.bar.ticks_per_beat);
    return static_cast<double>(pos.bar.bar_index) +
           beat_in_bar / static_cast<double>(pos.bar.beats_per_bar);
}

[[nodiscard]] double seconds_per_beat(
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    if (!tempo_map.valid()) {
        return 0.5;
    }
    const rhythm::RhythmPosition pos = tempo_map.position_from_microseconds(position_microseconds);
    const rhythm::ChartTick tick_now = pos.tick;
    if (pos.beat.ticks_per_beat <= 0) {
        return 0.5;
    }
    const rhythm::ChartTick tick_next_beat = static_cast<rhythm::ChartTick>(
        tick_now + pos.beat.ticks_per_beat - pos.beat.tick_offset_in_beat);
    const rhythm::TimelineMicroseconds next_us = tempo_map.microseconds_from_tick(tick_next_beat);
    const rhythm::TimelineMicroseconds now_us = tempo_map.microseconds_from_tick(tick_now);
    if (next_us <= now_us) {
        return 0.5;
    }
    const double seconds = static_cast<double>(next_us - now_us) / 1'000'000.0;
    return seconds * static_cast<double>(pos.beat.ticks_per_beat) /
        std::max(1.0, static_cast<double>(pos.beat.ticks_per_beat - pos.beat.tick_offset_in_beat));
}

[[nodiscard]] double seconds_per_unit(
    PulseEnvelopeUnit unit,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    // Returns the duration in seconds of one whole stride unit (one beat
    // for Beat-mode envelopes, one bar for Bar-mode envelopes) at the
    // CURRENT transport position. Reading the bar length directly from
    // tempo_map respects time-signature changes; the previous version
    // hardcoded 4 beats/bar which silently broke for any time signature
    // other than 4/4.
    if (!tempo_map.valid()) {
        return unit == PulseEnvelopeUnit::Beat ? 0.5 : 2.0;
    }
    const rhythm::RhythmPosition pos =
        tempo_map.position_from_microseconds(position_microseconds);
    if (pos.beat.ticks_per_beat <= 0) {
        return unit == PulseEnvelopeUnit::Beat ? 0.5 : 2.0;
    }
    const double sec_per_beat = seconds_per_beat(tempo_map, position_microseconds);
    if (unit == PulseEnvelopeUnit::Beat) {
        return sec_per_beat;
    }
    const int beats_per_bar = pos.bar.beats_per_bar > 0 ? pos.bar.beats_per_bar : 4;
    return sec_per_beat * static_cast<double>(beats_per_bar);
}

[[nodiscard]] double evaluate_attack_decay(
    double time_since_pulse_seconds, double attack_seconds, double decay_seconds) noexcept {
    if (time_since_pulse_seconds < 0.0) {
        return 0.0;
    }
    if (attack_seconds <= 0.0 && decay_seconds <= 0.0) {
        return 0.0;
    }
    if (time_since_pulse_seconds < attack_seconds) {
        return time_since_pulse_seconds / attack_seconds;
    }
    const double after_attack = time_since_pulse_seconds - attack_seconds;
    if (decay_seconds <= 0.0) {
        return 0.0;
    }
    if (after_attack >= decay_seconds) {
        return 0.0;
    }
    return 1.0 - after_attack / decay_seconds;
}

[[nodiscard]] double unit_position(
    PulseEnvelopeUnit unit,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    return unit == PulseEnvelopeUnit::Beat
        ? beats_position_for(tempo_map, position_microseconds)
        : bars_position_for(tempo_map, position_microseconds);
}

} // namespace

std::string_view to_string(PulseEnvelopeUnit unit) noexcept {
    switch (unit) {
    case PulseEnvelopeUnit::Beat:
        return "beat";
    case PulseEnvelopeUnit::Bar:
        return "bar";
    }
    return "unknown";
}

double evaluate_pulse_envelope(
    const PulseEnvelopeConfig& config,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    if (config.stride <= 0.0) {
        return 0.0;
    }
    const double position = unit_position(config.unit, tempo_map, position_microseconds) -
        config.phase_offset_beats;
    if (position < 0.0) {
        return 0.0;
    }
    const double stride_position = position / config.stride;
    const double fractional = stride_position - std::floor(stride_position);
    const double sec_per_unit = seconds_per_unit(config.unit, tempo_map, position_microseconds);
    const double seconds_since_pulse = fractional * config.stride * sec_per_unit;
    return std::clamp(
        evaluate_attack_decay(seconds_since_pulse, config.attack_seconds, config.decay_seconds),
        0.0,
        1.0);
}

double evaluate_square_wave_envelope(
    const SquareWaveEnvelopeConfig& config,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds) noexcept {
    if (config.stride <= 0.0) {
        return 0.0;
    }
    const double position = unit_position(config.unit, tempo_map, position_microseconds) -
        config.phase_offset_beats;
    const double stride_position = std::floor(position / config.stride);
    return (static_cast<std::int64_t>(stride_position) & 1) == 0 ? 1.0 : 0.0;
}

void evaluate_pulse_envelope_bulk(
    std::span<const PulseEnvelopeConfig> configs,
    std::span<const std::string_view> labels,
    const rhythm::TempoMap& tempo_map,
    rhythm::TimelineMicroseconds position_microseconds,
    std::vector<EnvelopeSample>& out_samples) {
    const std::size_t count = std::min(configs.size(), labels.size());
    for (std::size_t i = 0; i < count; ++i) {
        EnvelopeSample sample{};
        sample.value = evaluate_pulse_envelope(configs[i], tempo_map, position_microseconds);
        sample.label = labels[i];
        out_samples.push_back(sample);
    }
}

} // namespace reaktio::gameplay
