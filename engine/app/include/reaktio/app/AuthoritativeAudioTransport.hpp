#pragma once

#include "reaktio/app/TransportDrivenAudioSource.hpp"
#include "reaktio/audio/AudioClipLibrary.hpp"
#include "reaktio/gameplay/TransportController.hpp"
#include "reaktio/platform/SdlAudioDevice.hpp"

namespace reaktio::foundation {
class CrashSafeLog;
}

namespace reaktio::app {

class AuthoritativeAudioTransport final : public gameplay::ITransportControl {
  public:
    AuthoritativeAudioTransport() = default;

    bool bind_audio_clip(
        const audio::AudioClipRecord& clip,
        platform::SdlAudioDevice& device,
        foundation::CrashSafeLog& log);
    void unbind_audio_clip() noexcept;

    void tick(double simulation_delta_seconds) noexcept;

    [[nodiscard]] bool using_audio_authority() const noexcept;
    [[nodiscard]] platform::AudioPlaybackProgress playback_progress() const noexcept;
    [[nodiscard]] TransportDrivenAudioSnapshot clip_snapshot() const noexcept;

    [[nodiscard]] const gameplay::TransportSnapshot& snapshot() const noexcept override;
    void play() noexcept override;
    void pause() noexcept override;
    void stop() noexcept override;
    void restart() noexcept override;
    void seek(double position_seconds) noexcept override;
    void preview(double start_seconds, double end_seconds) noexcept override;
    void set_loop_region(double start_seconds, double end_seconds) noexcept override;
    void clear_loop_region() noexcept override;

  private:
    void sync_from_audio(bool use_device_position) noexcept;
    void update_device_stream_origin() noexcept;
    void pause_or_resume_device_for_current_state() noexcept;

    gameplay::TransportController transport_;
    TransportDrivenAudioSource clip_source_;
    platform::SdlAudioDevice* bound_device_{};
    bool using_audio_authority_{};
};

} // namespace reaktio::app