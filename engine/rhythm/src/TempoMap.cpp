#include "reaktio/rhythm/TempoMap.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace reaktio::rhythm {

namespace {

constexpr TimelineMicroseconds k_default_microseconds_per_quarter_note = 500000;

template <typename ValueType>
ValueType clamp_non_negative(ValueType value) noexcept {
    return std::max<ValueType>(value, 0);
}

std::int64_t ceil_div_positive(std::int64_t numerator, std::int64_t denominator) noexcept {
    if (numerator <= 0 || denominator <= 0) {
        return 0;
    }

    return 1 + (numerator - 1) / denominator;
}

bool normalize_tempo_changes(std::vector<TempoChange>& changes, std::string& error) {
    std::sort(
        changes.begin(),
        changes.end(),
        [](const TempoChange& lhs, const TempoChange& rhs) {
            return lhs.start_tick < rhs.start_tick;
        });

    std::vector<TempoChange> normalized;
    normalized.reserve(changes.size() + 1u);
    for (const TempoChange& change : changes) {
        if (change.start_tick < 0) {
            error = "Tempo changes must start at non-negative chart ticks.";
            return false;
        }

        if (change.microseconds_per_quarter_note <= 0) {
            error = "Tempo changes must use positive microseconds-per-quarter-note values.";
            return false;
        }

        if (!normalized.empty() && normalized.back().start_tick == change.start_tick) {
            normalized.back() = change;
            continue;
        }

        normalized.push_back(change);
    }

    if (normalized.empty() || normalized.front().start_tick != 0) {
        normalized.insert(
            normalized.begin(),
            TempoChange{
                .start_tick = 0,
                .microseconds_per_quarter_note = k_default_microseconds_per_quarter_note,
            });
    }

    changes = std::move(normalized);
    return true;
}

bool normalize_time_signature_changes(std::vector<TimeSignatureChange>& changes, std::string& error) {
    std::sort(
        changes.begin(),
        changes.end(),
        [](const TimeSignatureChange& lhs, const TimeSignatureChange& rhs) {
            return lhs.start_tick < rhs.start_tick;
        });

    std::vector<TimeSignatureChange> normalized;
    normalized.reserve(changes.size() + 1u);
    for (const TimeSignatureChange& change : changes) {
        if (change.start_tick < 0) {
            error = "Time-signature changes must start at non-negative chart ticks.";
            return false;
        }

        if (change.numerator <= 0 || change.denominator <= 0) {
            error = "Time-signature changes must use positive numerators and denominators.";
            return false;
        }

        if (!normalized.empty() && normalized.back().start_tick == change.start_tick) {
            normalized.back() = change;
            continue;
        }

        normalized.push_back(change);
    }

    if (normalized.empty() || normalized.front().start_tick != 0) {
        normalized.insert(
            normalized.begin(),
            TimeSignatureChange{
                .start_tick = 0,
                .numerator = 4,
                .denominator = 4,
            });
    }

    changes = std::move(normalized);
    return true;
}

bool normalize_stops(std::vector<StopSegment>& stops, std::string& error) {
    std::sort(
        stops.begin(),
        stops.end(),
        [](const StopSegment& lhs, const StopSegment& rhs) {
            return lhs.start_tick < rhs.start_tick;
        });

    std::vector<StopSegment> normalized;
    normalized.reserve(stops.size());
    for (const StopSegment& stop : stops) {
        if (stop.start_tick < 0) {
            error = "Stops must start at non-negative chart ticks.";
            return false;
        }

        if (stop.duration_microseconds < 0) {
            error = "Stops must use non-negative durations.";
            return false;
        }

        if (stop.duration_microseconds == 0) {
            continue;
        }

        if (!normalized.empty() && normalized.back().start_tick == stop.start_tick) {
            normalized.back().duration_microseconds += stop.duration_microseconds;
            continue;
        }

        normalized.push_back(stop);
    }

    stops = std::move(normalized);
    return true;
}

bool normalize_warps(std::vector<WarpSegment>& warps, std::string& error) {
    std::sort(
        warps.begin(),
        warps.end(),
        [](const WarpSegment& lhs, const WarpSegment& rhs) {
            return lhs.start_tick < rhs.start_tick;
        });

    std::vector<WarpSegment> normalized;
    normalized.reserve(warps.size());
    for (const WarpSegment& warp : warps) {
        if (warp.start_tick < 0) {
            error = "Warps must start at non-negative chart ticks.";
            return false;
        }

        if (warp.duration_ticks <= 0) {
            error = "Warps must use positive durations measured in chart ticks.";
            return false;
        }

        if (!normalized.empty() && normalized.back().start_tick == warp.start_tick) {
            normalized.back().duration_ticks += warp.duration_ticks;
            continue;
        }

        if (!normalized.empty() && warp.start_tick < normalized.back().start_tick + normalized.back().duration_ticks) {
            error = "Warp ranges must not overlap.";
            return false;
        }

        normalized.push_back(warp);
    }

    warps = std::move(normalized);
    return true;
}

TimelineMicroseconds sample_index_to_microseconds(AudioSampleIndex sample_index, std::int32_t sample_rate_hz) noexcept {
    if (sample_index <= 0 || sample_rate_hz <= 0) {
        return 0;
    }

    return (sample_index * microseconds_per_second()) / sample_rate_hz;
}

AudioSampleIndex microseconds_to_sample_index(TimelineMicroseconds microseconds, std::int32_t sample_rate_hz) noexcept {
    if (microseconds <= 0 || sample_rate_hz <= 0) {
        return 0;
    }

    return (microseconds * sample_rate_hz) / microseconds_per_second();
}

TimelineMicroseconds seconds_to_microseconds(double seconds) noexcept {
    if (!std::isfinite(seconds) || seconds <= 0.0) {
        return 0;
    }

    return static_cast<TimelineMicroseconds>(std::llround(seconds * static_cast<double>(microseconds_per_second())));
}

double microseconds_to_seconds(TimelineMicroseconds microseconds) noexcept {
    return static_cast<double>(microseconds) / static_cast<double>(microseconds_per_second());
}

} // namespace

TempoMap::TempoMap(TempoMapDefinition definition) {
    (void)rebuild(std::move(definition));
}

bool TempoMap::rebuild(TempoMapDefinition definition) {
    clear();

    if (definition.config.ticks_per_quarter_note <= 0) {
        last_error_ = "Tempo map requires a positive chart tick resolution.";
        return false;
    }

    if (definition.config.sample_rate_hz <= 0) {
        last_error_ = "Tempo map requires a positive audio sample rate.";
        return false;
    }

    if (!normalize_tempo_changes(definition.tempo_changes, last_error_) ||
        !normalize_time_signature_changes(definition.time_signature_changes, last_error_) ||
        !normalize_stops(definition.stops, last_error_) ||
        !normalize_warps(definition.warps, last_error_)) {
        return false;
    }

    definition_ = std::move(definition);
    if (!build_resolved_warps() || !build_resolved_timing_anchors() || !build_resolved_signatures()) {
        const std::string error_message = last_error_;
        clear();
        last_error_ = error_message;
        return false;
    }

    valid_ = true;
    return true;
}

void TempoMap::clear() noexcept {
    definition_ = {};
    resolved_warps_.clear();
    resolved_anchors_.clear();
    resolved_signature_regions_.clear();
    valid_ = false;
    last_error_.clear();
}

bool TempoMap::valid() const noexcept {
    return valid_;
}

std::string_view TempoMap::last_error() const noexcept {
    return last_error_;
}

const TempoMapDefinition& TempoMap::definition() const noexcept {
    return definition_;
}

const TempoMapConfig& TempoMap::config() const noexcept {
    return definition_.config;
}

BeatPosition TempoMap::beat_from_tick(ChartTick tick) const noexcept {
    const ChartTick clamped_tick = clamp_non_negative(tick);
    const ChartTick ticks_per_beat = std::max<ChartTick>(definition_.config.ticks_per_quarter_note, 1);
    return BeatPosition{
        .whole_beats = clamped_tick / ticks_per_beat,
        .tick_offset_in_beat = clamped_tick % ticks_per_beat,
        .ticks_per_beat = static_cast<std::int32_t>(ticks_per_beat),
    };
}

BarPosition TempoMap::bar_from_tick(ChartTick tick) const noexcept {
    const ChartTick clamped_tick = clamp_non_negative(tick);
    const ResolvedSignatureRegion* region = signature_region_for_tick(clamped_tick);
    if (region == nullptr || region->ticks_per_bar <= 0 || region->ticks_per_beat <= 0) {
        return {};
    }

    const ChartTick delta_ticks = clamped_tick - region->chart_tick_start;
    const std::int64_t bar_offset = delta_ticks / region->ticks_per_bar;
    const ChartTick tick_into_bar = delta_ticks % region->ticks_per_bar;
    return BarPosition{
        .bar_index = region->bar_index_start + bar_offset,
        .beat_index_in_bar = static_cast<std::int32_t>(tick_into_bar / region->ticks_per_beat),
        .tick_offset_in_beat = tick_into_bar % region->ticks_per_beat,
        .beats_per_bar = region->beats_per_bar,
        .beat_unit = region->beat_unit,
        .ticks_per_beat = static_cast<std::int32_t>(region->ticks_per_beat),
    };
}

ChartTick TempoMap::tick_from_beat(const BeatPosition& beat) const noexcept {
    const ChartTick ticks_per_beat = std::max<ChartTick>(definition_.config.ticks_per_quarter_note, 1);
    return std::max<ChartTick>(0, beat.whole_beats) * ticks_per_beat +
           std::clamp<ChartTick>(beat.tick_offset_in_beat, 0, ticks_per_beat - 1);
}

ChartTick TempoMap::tick_from_bar(const BarPosition& bar) const noexcept {
    const ResolvedSignatureRegion* region = signature_region_for_bar(std::max<std::int64_t>(bar.bar_index, 0));
    if (region == nullptr || region->ticks_per_bar <= 0 || region->ticks_per_beat <= 0) {
        return 0;
    }

    const std::int64_t bar_offset = std::max<std::int64_t>(0, bar.bar_index - region->bar_index_start);
    const ChartTick beat_offset = std::clamp<ChartTick>(
        static_cast<ChartTick>(bar.beat_index_in_bar),
        0,
        region->beats_per_bar - 1);
    const ChartTick tick_offset = std::clamp<ChartTick>(bar.tick_offset_in_beat, 0, region->ticks_per_beat - 1);
    return region->chart_tick_start + bar_offset * region->ticks_per_bar + beat_offset * region->ticks_per_beat +
           tick_offset;
}

TimelineMicroseconds TempoMap::microseconds_from_tick(ChartTick tick) const noexcept {
    if (!valid_) {
        return 0;
    }

    const ChartTick clamped_tick = clamp_non_negative(tick);
    const ResolvedAnchor* anchor = anchor_for_tick(clamped_tick);
    if (anchor == nullptr) {
        return 0;
    }

    if (clamped_tick == anchor->chart_tick) {
        return anchor->time_before_stop;
    }

    const ChartTick audible_tick = compress_tick(clamped_tick);
    const ChartTick audible_delta = std::max<ChartTick>(0, audible_tick - anchor->audible_tick);
    return anchor->time_after_stop +
           (audible_delta * anchor->tempo_microseconds_per_quarter_note) /
               std::max<ChartTick>(definition_.config.ticks_per_quarter_note, 1);
}

AudioSampleIndex TempoMap::samples_from_tick(ChartTick tick) const noexcept {
    return microseconds_to_sample_index(microseconds_from_tick(tick), definition_.config.sample_rate_hz);
}

double TempoMap::seconds_from_tick(ChartTick tick) const noexcept {
    return microseconds_to_seconds(microseconds_from_tick(tick));
}

ChartTick TempoMap::tick_from_microseconds(TimelineMicroseconds microseconds) const noexcept {
    if (!valid_ || resolved_anchors_.empty()) {
        return 0;
    }

    const TimelineMicroseconds clamped_time = clamp_non_negative(microseconds);
    ChartTick best_tick = 0;
    for (std::size_t index = 0; index < resolved_anchors_.size(); ++index) {
        const ResolvedAnchor& anchor = resolved_anchors_[index];
        if (anchor.time_before_stop <= clamped_time) {
            best_tick = anchor.chart_tick;
        }

        if (clamped_time < anchor.time_after_stop) {
            return anchor.chart_tick;
        }

        if (index + 1 >= resolved_anchors_.size()) {
            continue;
        }

        const ResolvedAnchor& next_anchor = resolved_anchors_[index + 1];
        if (clamped_time < next_anchor.time_before_stop) {
            if (next_anchor.time_before_stop <= anchor.time_after_stop) {
                continue;
            }

            const TimelineMicroseconds delta_time = clamped_time - anchor.time_after_stop;
            const ChartTick delta_ticks = (delta_time * definition_.config.ticks_per_quarter_note) /
                std::max<TimelineMicroseconds>(anchor.tempo_microseconds_per_quarter_note, 1);
            return std::clamp(anchor.chart_tick + delta_ticks, anchor.chart_tick, next_anchor.chart_tick);
        }
    }

    const ResolvedAnchor& last_anchor = resolved_anchors_.back();
    if (clamped_time < last_anchor.time_after_stop) {
        return last_anchor.chart_tick;
    }

    const TimelineMicroseconds delta_time = clamped_time - last_anchor.time_after_stop;
    const ChartTick delta_ticks = (delta_time * definition_.config.ticks_per_quarter_note) /
        std::max<TimelineMicroseconds>(last_anchor.tempo_microseconds_per_quarter_note, 1);
    return last_anchor.chart_tick + delta_ticks;
}

ChartTick TempoMap::tick_from_samples(AudioSampleIndex sample_index) const noexcept {
    return tick_from_microseconds(sample_index_to_microseconds(sample_index, definition_.config.sample_rate_hz));
}

ChartTick TempoMap::tick_from_seconds(double seconds) const noexcept {
    return tick_from_microseconds(seconds_to_microseconds(seconds));
}

RhythmPosition TempoMap::position_from_tick(ChartTick tick) const noexcept {
    const ChartTick clamped_tick = clamp_non_negative(tick);
    const TimelineMicroseconds microseconds = microseconds_from_tick(clamped_tick);
    return make_position(
        clamped_tick,
        microseconds,
        microseconds_to_sample_index(microseconds, definition_.config.sample_rate_hz),
        microseconds_to_seconds(microseconds));
}

RhythmPosition TempoMap::position_from_microseconds(TimelineMicroseconds microseconds) const noexcept {
    const TimelineMicroseconds clamped_time = clamp_non_negative(microseconds);
    const ChartTick tick = tick_from_microseconds(clamped_time);
    return make_position(
        tick,
        clamped_time,
        microseconds_to_sample_index(clamped_time, definition_.config.sample_rate_hz),
        microseconds_to_seconds(clamped_time));
}

RhythmPosition TempoMap::position_from_samples(AudioSampleIndex sample_index) const noexcept {
    const AudioSampleIndex clamped_samples = clamp_non_negative(sample_index);
    const TimelineMicroseconds microseconds = sample_index_to_microseconds(clamped_samples, definition_.config.sample_rate_hz);
    return make_position(
        tick_from_microseconds(microseconds),
        microseconds,
        clamped_samples,
        microseconds_to_seconds(microseconds));
}

RhythmPosition TempoMap::position_from_seconds(double seconds) const noexcept {
    const TimelineMicroseconds microseconds = seconds_to_microseconds(seconds);
    return make_position(
        tick_from_microseconds(microseconds),
        microseconds,
        microseconds_to_sample_index(microseconds, definition_.config.sample_rate_hz),
        microseconds_to_seconds(microseconds));
}

ChartTick TempoMap::compress_tick(ChartTick tick) const noexcept {
    const ChartTick clamped_tick = clamp_non_negative(tick);
    ChartTick skipped_ticks = 0;
    for (const ResolvedWarpSegment& warp : resolved_warps_) {
        if (clamped_tick < warp.start_tick) {
            break;
        }

        if (clamped_tick < warp.end_tick) {
            return warp.audible_tick_at_start;
        }

        skipped_ticks = warp.skipped_ticks_before_start + (warp.end_tick - warp.start_tick);
    }

    return clamped_tick - skipped_ticks;
}

const TempoMap::ResolvedAnchor* TempoMap::anchor_for_tick(ChartTick tick) const noexcept {
    if (resolved_anchors_.empty()) {
        return nullptr;
    }

    auto it = std::upper_bound(
        resolved_anchors_.begin(),
        resolved_anchors_.end(),
        tick,
        [](ChartTick target_tick, const ResolvedAnchor& anchor) {
            return target_tick < anchor.chart_tick;
        });
    if (it == resolved_anchors_.begin()) {
        return &resolved_anchors_.front();
    }

    --it;
    return &(*it);
}

const TempoMap::ResolvedSignatureRegion* TempoMap::signature_region_for_tick(ChartTick tick) const noexcept {
    if (resolved_signature_regions_.empty()) {
        return nullptr;
    }

    auto it = std::upper_bound(
        resolved_signature_regions_.begin(),
        resolved_signature_regions_.end(),
        tick,
        [](ChartTick target_tick, const ResolvedSignatureRegion& region) {
            return target_tick < region.chart_tick_start;
        });
    if (it == resolved_signature_regions_.begin()) {
        return &resolved_signature_regions_.front();
    }

    --it;
    return &(*it);
}

const TempoMap::ResolvedSignatureRegion* TempoMap::signature_region_for_bar(std::int64_t bar_index) const noexcept {
    for (const ResolvedSignatureRegion& region : resolved_signature_regions_) {
        if (bar_index >= region.bar_index_start && bar_index < region.bar_index_end_exclusive) {
            return &region;
        }
    }

    return resolved_signature_regions_.empty() ? nullptr : &resolved_signature_regions_.back();
}

RhythmPosition TempoMap::make_position(
    ChartTick tick,
    TimelineMicroseconds microseconds,
    AudioSampleIndex sample_index,
    double seconds) const noexcept {
    return RhythmPosition{
        .tick = tick,
        .microseconds = microseconds,
        .sample_index = sample_index,
        .seconds = seconds,
        .beat = beat_from_tick(tick),
        .bar = bar_from_tick(tick),
    };
}

bool TempoMap::build_resolved_warps() {
    resolved_warps_.clear();
    resolved_warps_.reserve(definition_.warps.size());

    ChartTick skipped_ticks = 0;
    for (const WarpSegment& warp : definition_.warps) {
        resolved_warps_.push_back(ResolvedWarpSegment{
            .start_tick = warp.start_tick,
            .end_tick = warp.start_tick + warp.duration_ticks,
            .skipped_ticks_before_start = skipped_ticks,
            .audible_tick_at_start = warp.start_tick - skipped_ticks,
        });
        skipped_ticks += warp.duration_ticks;
    }

    return true;
}

bool TempoMap::build_resolved_timing_anchors() {
    resolved_anchors_.clear();

    std::vector<ChartTick> anchor_ticks;
    anchor_ticks.reserve(
        1u + definition_.tempo_changes.size() + definition_.stops.size() + definition_.warps.size() * 2u);
    anchor_ticks.push_back(0);
    for (const TempoChange& change : definition_.tempo_changes) {
        anchor_ticks.push_back(change.start_tick);
    }
    for (const StopSegment& stop : definition_.stops) {
        anchor_ticks.push_back(stop.start_tick);
    }
    for (const WarpSegment& warp : definition_.warps) {
        anchor_ticks.push_back(warp.start_tick);
        anchor_ticks.push_back(warp.start_tick + warp.duration_ticks);
    }

    std::sort(anchor_ticks.begin(), anchor_ticks.end());
    anchor_ticks.erase(std::unique(anchor_ticks.begin(), anchor_ticks.end()), anchor_ticks.end());

    std::size_t tempo_index = 0;
    std::size_t stop_index = 0;
    resolved_anchors_.reserve(anchor_ticks.size());
    for (std::size_t anchor_index = 0; anchor_index < anchor_ticks.size(); ++anchor_index) {
        const ChartTick anchor_tick = anchor_ticks[anchor_index];
        while (tempo_index + 1u < definition_.tempo_changes.size() &&
               definition_.tempo_changes[tempo_index + 1u].start_tick <= anchor_tick) {
            ++tempo_index;
        }

        const ChartTick audible_tick = compress_tick(anchor_tick);
        TimelineMicroseconds time_before_stop = 0;
        if (!resolved_anchors_.empty()) {
            const ResolvedAnchor& previous_anchor = resolved_anchors_.back();
            const ChartTick audible_delta = audible_tick - previous_anchor.audible_tick;
            if (audible_delta < 0) {
                last_error_ = "Tempo map produced a negative audible tick delta while resolving anchors.";
                return false;
            }

            time_before_stop = previous_anchor.time_after_stop +
                (audible_delta * previous_anchor.tempo_microseconds_per_quarter_note) /
                    definition_.config.ticks_per_quarter_note;
        }

        TimelineMicroseconds stop_duration = 0;
        while (stop_index < definition_.stops.size() && definition_.stops[stop_index].start_tick < anchor_tick) {
            ++stop_index;
        }
        while (stop_index < definition_.stops.size() && definition_.stops[stop_index].start_tick == anchor_tick) {
            stop_duration += definition_.stops[stop_index].duration_microseconds;
            ++stop_index;
        }

        resolved_anchors_.push_back(ResolvedAnchor{
            .chart_tick = anchor_tick,
            .audible_tick = audible_tick,
            .time_before_stop = time_before_stop,
            .time_after_stop = time_before_stop + stop_duration,
            .tempo_microseconds_per_quarter_note = definition_.tempo_changes[tempo_index].microseconds_per_quarter_note,
        });
    }

    return true;
}

bool TempoMap::build_resolved_signatures() {
    resolved_signature_regions_.clear();
    resolved_signature_regions_.reserve(definition_.time_signature_changes.size());

    std::int64_t bar_index_start = 0;
    for (std::size_t index = 0; index < definition_.time_signature_changes.size(); ++index) {
        const TimeSignatureChange& change = definition_.time_signature_changes[index];
        const ChartTick ticks_per_beat_numerator = static_cast<ChartTick>(definition_.config.ticks_per_quarter_note) * 4;
        if (ticks_per_beat_numerator % change.denominator != 0) {
            last_error_ = "Time-signature denominator must divide four quarter-notes worth of chart ticks.";
            return false;
        }

        const ChartTick ticks_per_beat = ticks_per_beat_numerator / change.denominator;
        const ChartTick ticks_per_bar = ticks_per_beat * change.numerator;
        if (ticks_per_bar <= 0) {
            last_error_ = "Time-signature changes must yield a positive number of chart ticks per bar.";
            return false;
        }

        if (!resolved_signature_regions_.empty()) {
            const ResolvedSignatureRegion& previous_region = resolved_signature_regions_.back();
            bar_index_start = previous_region.bar_index_start +
                ceil_div_positive(change.start_tick - previous_region.chart_tick_start, previous_region.ticks_per_bar);
            resolved_signature_regions_.back().chart_tick_end_exclusive = change.start_tick;
            resolved_signature_regions_.back().bar_index_end_exclusive = bar_index_start;
        }

        resolved_signature_regions_.push_back(ResolvedSignatureRegion{
            .chart_tick_start = change.start_tick,
            .chart_tick_end_exclusive = std::numeric_limits<ChartTick>::max(),
            .bar_index_start = bar_index_start,
            .bar_index_end_exclusive = std::numeric_limits<std::int64_t>::max(),
            .beats_per_bar = change.numerator,
            .beat_unit = change.denominator,
            .ticks_per_beat = ticks_per_beat,
            .ticks_per_bar = ticks_per_bar,
        });
    }

    return true;
}

std::int64_t microseconds_per_quarter_from_milli_bpm(std::int64_t milli_beats_per_minute) noexcept {
    if (milli_beats_per_minute <= 0) {
        return 0;
    }

    return 60000000000ll / milli_beats_per_minute;
}

double beats_per_minute_from_microseconds_per_quarter(
    TimelineMicroseconds microseconds_per_quarter_note) noexcept {
    if (microseconds_per_quarter_note <= 0) {
        return 0.0;
    }

    return 60000000.0 / static_cast<double>(microseconds_per_quarter_note);
}

} // namespace reaktio::rhythm