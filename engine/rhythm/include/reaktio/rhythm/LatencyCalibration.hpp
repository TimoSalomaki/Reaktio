#pragma once

#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reaktio::rhythm {

enum class LatencyCalibrationKind : std::uint8_t {
    AudioOutput,
    InputResponse,
};

struct LatencyCalibrationObservation {
    LatencyCalibrationKind kind{LatencyCalibrationKind::AudioOutput};
    TimelineMicroseconds recommended_offset_microseconds{};
    TimelineMicroseconds reference_time_microseconds{};
    TimelineMicroseconds observed_time_microseconds{};
};

struct LatencyCalibrationSummary {
    std::size_t sample_count{};
    TimelineMicroseconds recommended_offset_microseconds{};
    TimelineMicroseconds median_offset_microseconds{};
    TimelineMicroseconds mean_absolute_deviation_microseconds{};
    TimelineMicroseconds min_offset_microseconds{};
    TimelineMicroseconds max_offset_microseconds{};
    bool stable{};
};

class LatencyCalibrationSession {
  public:
    explicit LatencyCalibrationSession(LatencyCalibrationKind kind = LatencyCalibrationKind::AudioOutput) noexcept;

    [[nodiscard]] LatencyCalibrationKind kind() const noexcept;
    [[nodiscard]] bool add_observation(const LatencyCalibrationObservation& observation) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::span<const LatencyCalibrationObservation> observations() const noexcept;
    [[nodiscard]] const LatencyCalibrationSummary& summary() const noexcept;

  private:
    void rebuild_summary() noexcept;

    LatencyCalibrationKind kind_{LatencyCalibrationKind::AudioOutput};
    std::vector<LatencyCalibrationObservation> observations_;
    LatencyCalibrationSummary summary_{};
};

[[nodiscard]] LatencyCalibrationObservation make_audio_output_calibration_observation(
    TimelineMicroseconds recommended_offset_microseconds) noexcept;
[[nodiscard]] LatencyCalibrationObservation make_input_response_calibration_observation(
    TimelineMicroseconds cue_time_microseconds,
    TimelineMicroseconds input_time_microseconds,
    const TimingOffsetProfile& current_offset_profile) noexcept;

[[nodiscard]] inline constexpr std::string_view to_string(LatencyCalibrationKind kind) noexcept {
    switch (kind) {
    case LatencyCalibrationKind::AudioOutput:
        return "audio-output";
    case LatencyCalibrationKind::InputResponse:
        return "input-response";
    }

    return "unknown";
}

} // namespace reaktio::rhythm