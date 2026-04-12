#pragma once

#include "reaktio/audio/AudioClipLibrary.hpp"
#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/platform/AudioStreamSource.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::app {

struct TransportDrivenAudioSnapshot {
    bool ready{};
    bool finished{true};
    std::uint64_t total_frames{};
    std::uint64_t rendered_input_frames{};
    gameplay::TransportSnapshot transport_snapshot{};
};

class TransportDrivenAudioSource final : public platform::IAudioStreamSource {
  public:
    bool bind_clip(
        const audio::AudioClipRecord& clip,
        const platform::AudioSpec& input_spec,
        foundation::CrashSafeLog& log);
    void play() noexcept;
    void pause() noexcept;
    void reset() noexcept;
    void stop() noexcept;
    void restart() noexcept;
    void seek(double position_seconds) noexcept;
    void preview(double start_seconds, double end_seconds) noexcept;
    void set_loop_region(double start_seconds, double end_seconds) noexcept;
    void clear_loop_region() noexcept;

    [[nodiscard]] TransportDrivenAudioSnapshot snapshot() const noexcept;

    int render_audio_frames(
        std::byte* destination,
        int requested_frames,
        const platform::AudioSpec& input_spec) noexcept override;

  private:
    static bool position_changed(double lhs, double rhs) noexcept;

    [[nodiscard]] double seconds_from_frame(std::uint64_t frame_index) const noexcept;
    [[nodiscard]] std::uint64_t frame_from_seconds(double seconds) const noexcept;
    [[nodiscard]] std::uint64_t current_loop_start_frame() const noexcept;
    [[nodiscard]] std::uint64_t current_loop_end_frame() const noexcept;
    [[nodiscard]] std::uint64_t current_preview_start_frame() const noexcept;
    [[nodiscard]] std::uint64_t current_preview_end_frame() const noexcept;

    void set_rendered_frame(std::uint64_t frame_index) noexcept;
    void clear_preview_region() noexcept;
    void record_discontinuity(
        gameplay::TransportDiscontinuityReason reason,
        double from_seconds,
        double to_seconds) noexcept;
    void handle_render_boundary() noexcept;

    mutable std::mutex mutex_;
    platform::AudioSpec input_spec_{};
    std::vector<std::byte> converted_bytes_;
    std::uint64_t total_frames_{};
    std::uint64_t rendered_frames_{};
    gameplay::TransportSnapshot transport_snapshot_{};
    bool ready_{};
    bool finished_{true};
};

} // namespace reaktio::app