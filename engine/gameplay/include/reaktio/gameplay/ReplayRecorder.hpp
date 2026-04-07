#pragma once

#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/platform/FrameClock.hpp"
#include "reaktio/platform/InputSnapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

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
    double transport_position_seconds{};
    std::uint64_t root_random_seed{};
    std::uint64_t authoritative_state_hash{};
    std::string label;
    std::string summary;
};

class ReplayRecorder {
  public:
    void begin_session(ReplaySessionMetadata metadata);
    void reset() noexcept;

    void record_input_frame(const platform::FrameTiming& frame_timing, const platform::InputSnapshot& input_snapshot);
    void record_checkpoint(ReplayCheckpoint checkpoint);

    [[nodiscard]] const ReplaySessionMetadata* session() const noexcept;
    [[nodiscard]] const ReplayInputFrame* last_input_frame() const noexcept;
    [[nodiscard]] const ReplayCheckpoint* last_checkpoint() const noexcept;
    [[nodiscard]] std::size_t input_frame_count() const noexcept;
    [[nodiscard]] std::size_t checkpoint_count() const noexcept;

  private:
    static constexpr std::size_t k_max_input_frames = 256;
    static constexpr std::size_t k_max_checkpoints = 128;

    ReplaySessionMetadata session_{};
    bool session_active_{false};
    std::deque<ReplayInputFrame> input_frames_;
    std::deque<ReplayCheckpoint> checkpoints_;
};

} // namespace reaktio::gameplay