#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::rhythm {

using ChartTick = std::int64_t;
using TimelineMicroseconds = std::int64_t;
using AudioSampleIndex = std::int64_t;

struct TempoMapConfig {
    std::int32_t ticks_per_quarter_note{480};
    std::int32_t sample_rate_hz{48000};
};

struct TempoChange {
    ChartTick start_tick{};
    TimelineMicroseconds microseconds_per_quarter_note{500000};
};

struct TimeSignatureChange {
    ChartTick start_tick{};
    std::int32_t numerator{4};
    std::int32_t denominator{4};
};

struct StopSegment {
    ChartTick start_tick{};
    TimelineMicroseconds duration_microseconds{};
};

struct WarpSegment {
    ChartTick start_tick{};
    ChartTick duration_ticks{};
};

struct TempoMapDefinition {
    TempoMapConfig config{};
    std::vector<TempoChange> tempo_changes;
    std::vector<TimeSignatureChange> time_signature_changes;
    std::vector<StopSegment> stops;
    std::vector<WarpSegment> warps;
};

struct BeatPosition {
    std::int64_t whole_beats{};
    ChartTick tick_offset_in_beat{};
    std::int32_t ticks_per_beat{};
};

struct BarPosition {
    std::int64_t bar_index{};
    std::int32_t beat_index_in_bar{};
    ChartTick tick_offset_in_beat{};
    std::int32_t beats_per_bar{};
    std::int32_t beat_unit{};
    std::int32_t ticks_per_beat{};
};

struct RhythmPosition {
    ChartTick tick{};
    TimelineMicroseconds microseconds{};
    AudioSampleIndex sample_index{};
    double seconds{};
    BeatPosition beat{};
    BarPosition bar{};
};

class TempoMap {
  public:
    TempoMap() = default;
    explicit TempoMap(TempoMapDefinition definition);

    bool rebuild(TempoMapDefinition definition);
    void clear() noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    [[nodiscard]] const TempoMapDefinition& definition() const noexcept;
    [[nodiscard]] const TempoMapConfig& config() const noexcept;

    [[nodiscard]] BeatPosition beat_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] BarPosition bar_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] ChartTick tick_from_beat(const BeatPosition& beat) const noexcept;
    [[nodiscard]] ChartTick tick_from_bar(const BarPosition& bar) const noexcept;

    [[nodiscard]] TimelineMicroseconds microseconds_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] AudioSampleIndex samples_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] double seconds_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] ChartTick tick_from_microseconds(TimelineMicroseconds microseconds) const noexcept;
    [[nodiscard]] ChartTick tick_from_samples(AudioSampleIndex sample_index) const noexcept;
    [[nodiscard]] ChartTick tick_from_seconds(double seconds) const noexcept;

    [[nodiscard]] RhythmPosition position_from_tick(ChartTick tick) const noexcept;
    [[nodiscard]] RhythmPosition position_from_microseconds(TimelineMicroseconds microseconds) const noexcept;
    [[nodiscard]] RhythmPosition position_from_samples(AudioSampleIndex sample_index) const noexcept;
    [[nodiscard]] RhythmPosition position_from_seconds(double seconds) const noexcept;

  private:
    struct ResolvedWarpSegment {
        ChartTick start_tick{};
        ChartTick end_tick{};
        ChartTick skipped_ticks_before_start{};
        ChartTick audible_tick_at_start{};
    };

    struct ResolvedAnchor {
        ChartTick chart_tick{};
        ChartTick audible_tick{};
        TimelineMicroseconds time_before_stop{};
        TimelineMicroseconds time_after_stop{};
        TimelineMicroseconds tempo_microseconds_per_quarter_note{500000};
    };

    struct ResolvedSignatureRegion {
        ChartTick chart_tick_start{};
        ChartTick chart_tick_end_exclusive{std::numeric_limits<ChartTick>::max()};
        std::int64_t bar_index_start{};
        std::int64_t bar_index_end_exclusive{std::numeric_limits<std::int64_t>::max()};
        std::int32_t beats_per_bar{4};
        std::int32_t beat_unit{4};
        ChartTick ticks_per_beat{};
        ChartTick ticks_per_bar{};
    };

    [[nodiscard]] ChartTick compress_tick(ChartTick tick) const noexcept;
    [[nodiscard]] const ResolvedAnchor* anchor_for_tick(ChartTick tick) const noexcept;
    [[nodiscard]] const ResolvedSignatureRegion* signature_region_for_tick(ChartTick tick) const noexcept;
    [[nodiscard]] const ResolvedSignatureRegion* signature_region_for_bar(std::int64_t bar_index) const noexcept;
    [[nodiscard]] RhythmPosition make_position(
        ChartTick tick,
        TimelineMicroseconds microseconds,
        AudioSampleIndex sample_index,
        double seconds) const noexcept;

    bool build_resolved_warps();
    bool build_resolved_timing_anchors();
    bool build_resolved_signatures();

    TempoMapDefinition definition_{};
    std::vector<ResolvedWarpSegment> resolved_warps_;
    std::vector<ResolvedAnchor> resolved_anchors_;
    std::vector<ResolvedSignatureRegion> resolved_signature_regions_;
    bool valid_{};
    std::string last_error_{};
};

[[nodiscard]] constexpr TimelineMicroseconds microseconds_per_second() noexcept {
    return 1000000;
}

[[nodiscard]] std::int64_t microseconds_per_quarter_from_milli_bpm(std::int64_t milli_beats_per_minute) noexcept;
[[nodiscard]] double beats_per_minute_from_microseconds_per_quarter(
    TimelineMicroseconds microseconds_per_quarter_note) noexcept;

} // namespace reaktio::rhythm