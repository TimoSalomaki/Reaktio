#include "reaktio/rhythm/LatencyCalibration.hpp"

#include <algorithm>
#include <cstdlib>

namespace reaktio::rhythm {

namespace {

constexpr TimelineMicroseconds k_stable_mean_absolute_deviation_microseconds = 6000;
constexpr std::size_t k_stable_sample_count = 4;

TimelineMicroseconds median_of_sorted_offsets(const std::vector<TimelineMicroseconds>& sorted_offsets) noexcept {
    if (sorted_offsets.empty()) {
        return 0;
    }

    const std::size_t middle_index = sorted_offsets.size() / 2u;
    if ((sorted_offsets.size() % 2u) != 0u) {
        return sorted_offsets[middle_index];
    }

    return (sorted_offsets[middle_index - 1u] + sorted_offsets[middle_index]) / 2;
}

} // namespace

LatencyCalibrationSession::LatencyCalibrationSession(LatencyCalibrationKind kind) noexcept
    : kind_(kind) {}

LatencyCalibrationKind LatencyCalibrationSession::kind() const noexcept {
    return kind_;
}

bool LatencyCalibrationSession::add_observation(const LatencyCalibrationObservation& observation) noexcept {
    if (observation.kind != kind_) {
        return false;
    }

    observations_.push_back(observation);
    rebuild_summary();
    return true;
}

void LatencyCalibrationSession::clear() noexcept {
    observations_.clear();
    summary_ = {};
}

std::span<const LatencyCalibrationObservation> LatencyCalibrationSession::observations() const noexcept {
    return std::span<const LatencyCalibrationObservation>{observations_.data(), observations_.size()};
}

const LatencyCalibrationSummary& LatencyCalibrationSession::summary() const noexcept {
    return summary_;
}

void LatencyCalibrationSession::rebuild_summary() noexcept {
    summary_ = {};
    if (observations_.empty()) {
        return;
    }

    std::vector<TimelineMicroseconds> offsets;
    offsets.reserve(observations_.size());
    summary_.sample_count = observations_.size();
    summary_.min_offset_microseconds = observations_.front().recommended_offset_microseconds;
    summary_.max_offset_microseconds = observations_.front().recommended_offset_microseconds;
    for (const LatencyCalibrationObservation& observation : observations_) {
        offsets.push_back(observation.recommended_offset_microseconds);
        summary_.min_offset_microseconds = std::min(summary_.min_offset_microseconds, observation.recommended_offset_microseconds);
        summary_.max_offset_microseconds = std::max(summary_.max_offset_microseconds, observation.recommended_offset_microseconds);
    }

    std::sort(offsets.begin(), offsets.end());
    summary_.median_offset_microseconds = median_of_sorted_offsets(offsets);
    summary_.recommended_offset_microseconds = summary_.median_offset_microseconds;

    TimelineMicroseconds absolute_deviation_total = 0;
    for (TimelineMicroseconds offset : offsets) {
        absolute_deviation_total += std::llabs(offset - summary_.median_offset_microseconds);
    }
    summary_.mean_absolute_deviation_microseconds =
        absolute_deviation_total / static_cast<TimelineMicroseconds>(offsets.size());
    summary_.stable = summary_.sample_count >= k_stable_sample_count &&
        summary_.mean_absolute_deviation_microseconds <= k_stable_mean_absolute_deviation_microseconds;
}

LatencyCalibrationObservation make_audio_output_calibration_observation(
    TimelineMicroseconds recommended_offset_microseconds) noexcept {
    return LatencyCalibrationObservation{
        .kind = LatencyCalibrationKind::AudioOutput,
        .recommended_offset_microseconds = recommended_offset_microseconds,
    };
}

LatencyCalibrationObservation make_input_response_calibration_observation(
    TimelineMicroseconds cue_time_microseconds,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& current_offset_profile) noexcept {
    TimingOffsetProfile baseline_offset_profile = current_offset_profile;
    baseline_offset_profile.input_response_offset_microseconds = 0;
    const TimelineMicroseconds recommended_offset_microseconds = input_time_microseconds -
        (cue_time_microseconds + total_timing_offset(baseline_offset_profile));
    return LatencyCalibrationObservation{
        .kind = LatencyCalibrationKind::InputResponse,
        .recommended_offset_microseconds = recommended_offset_microseconds,
        .reference_time_microseconds = cue_time_microseconds,
        .observed_time_microseconds = input_time_microseconds,
    };
}

} // namespace reaktio::rhythm