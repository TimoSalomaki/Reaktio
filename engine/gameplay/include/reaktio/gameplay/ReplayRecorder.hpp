#pragma once

#include "reaktio/gameplay/Scoring.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/platform/InputSnapshot.hpp"
#include "reaktio/rhythm/TimingJudgement.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace reaktio::platform {
struct FrameTiming;
} // namespace reaktio::platform

namespace reaktio::gameplay {

struct ReplaySessionMetadata {
    std::string mode_id;
    std::string mode_display_name;
    std::uint64_t root_random_seed{};
};

struct ReplayInputFrame {
    std::uint64_t frame_index{};
    std::uint64_t wall_clock_ns{};
    double frame_delta_seconds{};
    double interpolation_alpha{};
    std::vector<platform::KeyboardEvent> keyboard_events;
    std::vector<platform::TextEditingEvent> text_editing_events;
    std::vector<platform::TextEditingCandidatesEvent> text_editing_candidates_events;
    std::vector<platform::TextInputEvent> text_input_events;
    std::vector<platform::MouseMotionEvent> mouse_motion_events;
    std::vector<platform::MouseButtonEvent> mouse_button_events;
    std::vector<platform::MouseWheelEvent> mouse_wheel_events;
    std::vector<platform::GamepadConnectionEvent> gamepad_connection_events;
    std::vector<platform::GamepadAxisEvent> gamepad_axis_events;
    std::vector<platform::GamepadButtonEvent> gamepad_button_events;
};

struct ReplayCheckpoint {
    std::uint64_t frame_index{};
    std::uint64_t simulation_step{};
    TransportPlaybackState transport_state{TransportPlaybackState::Stopped};
  TransportPositionAuthority transport_position_authority{TransportPositionAuthority::Simulation};
    double transport_position_seconds{};
  std::uint64_t transport_timeline_revision{};
  TransportDiscontinuityReason transport_discontinuity_reason{TransportDiscontinuityReason::None};
    std::uint64_t root_random_seed{};
    std::uint64_t authoritative_state_hash{};
    std::string label;
    std::string summary;
};

struct ReplayJudgementSample {
    std::uint64_t frame_index{};
    std::uint64_t simulation_step{};
    std::uint64_t cue_id{};
    std::size_t schedule_index{};
    rhythm::ChartTick cue_hit_tick{};
    std::uint32_t channel_index{};
    rhythm::TimingJudgement judgement{rhythm::TimingJudgement::Miss};
    bool scoreable_hit{};
    bool advances_combo{};
    rhythm::TimelineMicroseconds raw_error_microseconds{};
    rhythm::TimelineMicroseconds corrected_error_microseconds{};
    rhythm::TimelineMicroseconds applied_offset_microseconds{};
    std::uint64_t score_after{};
    std::uint32_t combo_after{};
    double multiplier_after{1.0};
    double health_after{1.0};
    ScoreRunState run_state{ScoreRunState::Active};
};

class ReplayRecorder {
  public:
    static constexpr std::size_t k_max_input_frames = 256;
    static constexpr std::size_t k_max_checkpoints = 128;
    static constexpr std::size_t k_max_judgement_samples = 1024;

    void begin_session(ReplaySessionMetadata metadata);
    void reset() noexcept;

    void record_input_frame(const platform::FrameTiming& frame_timing, const platform::InputSnapshot& input_snapshot);
    void record_checkpoint(ReplayCheckpoint checkpoint);
    void record_judgement_sample(ReplayJudgementSample sample);

    [[nodiscard]] const ReplaySessionMetadata* session() const noexcept;
    [[nodiscard]] const ReplayInputFrame* last_input_frame() const noexcept;
    [[nodiscard]] const ReplayCheckpoint* last_checkpoint() const noexcept;
    [[nodiscard]] const ReplayJudgementSample* last_judgement_sample() const noexcept;
    [[nodiscard]] std::size_t input_frame_count() const noexcept;
    [[nodiscard]] std::size_t checkpoint_count() const noexcept;
    [[nodiscard]] std::size_t judgement_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t total_input_frames_recorded() const noexcept;
    [[nodiscard]] std::uint64_t total_checkpoints_recorded() const noexcept;
    [[nodiscard]] std::uint64_t total_judgement_samples_recorded() const noexcept;
    [[nodiscard]] const std::deque<ReplayInputFrame>& input_frames() const noexcept;
    [[nodiscard]] const std::deque<ReplayCheckpoint>& checkpoints() const noexcept;
    [[nodiscard]] const std::deque<ReplayJudgementSample>& judgement_samples() const noexcept;

  private:
    ReplaySessionMetadata session_{};
    bool session_active_{false};
    std::deque<ReplayInputFrame> input_frames_;
    std::deque<ReplayCheckpoint> checkpoints_;
    std::deque<ReplayJudgementSample> judgement_samples_;
    std::uint64_t total_input_frames_recorded_{};
    std::uint64_t total_checkpoints_recorded_{};
    std::uint64_t total_judgement_samples_recorded_{};
};

struct ReplaySession {
    ReplaySessionMetadata metadata;
    std::vector<ReplayInputFrame> input_frames;
    std::vector<ReplayCheckpoint> checkpoints;
    std::vector<ReplayJudgementSample> judgement_samples;
    std::uint64_t total_input_frames_recorded{};
    std::uint64_t total_checkpoints_recorded{};
    std::uint64_t total_judgement_samples_recorded{};
    bool input_frames_truncated{};
    bool checkpoints_truncated{};
    bool judgement_samples_truncated{};
};

[[nodiscard]] ReplaySession make_replay_session(const ReplayRecorder& recorder);

} // namespace reaktio::gameplay