#include "reaktio/app/AuthoritativeAudioTransport.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#include <algorithm>
#include <cmath>

namespace {

double wrap_seconds(double relative_seconds, double length_seconds) noexcept {
    if (length_seconds <= 0.0) {
        return 0.0;
    }

    double wrapped = std::fmod(relative_seconds, length_seconds);
    if (wrapped < 0.0) {
        wrapped += length_seconds;
    }

    return wrapped;
}

double resolve_authoritative_transport_position(
    const reaktio::gameplay::TransportSnapshot& snapshot,
    const reaktio::platform::AudioPlaybackProgress& progress) noexcept {
    const double total_output_latency = progress.total_output_latency_seconds;

    if (snapshot.playback_mode == reaktio::gameplay::TransportPlaybackMode::Preview && snapshot.preview_region.enabled) {
        return std::clamp(
            snapshot.position_seconds - total_output_latency,
            snapshot.preview_region.start_seconds,
            snapshot.preview_region.end_seconds);
    }

    if (snapshot.loop_region.enabled) {
        const double loop_start = snapshot.loop_region.start_seconds;
        const double loop_end = snapshot.loop_region.end_seconds;
        const double loop_length = loop_end - loop_start;
        if (loop_length > 0.0) {
            return loop_start + wrap_seconds(snapshot.position_seconds - loop_start - total_output_latency, loop_length);
        }
    }

    return std::clamp(
        snapshot.position_seconds - total_output_latency,
        0.0,
        snapshot.duration_seconds);
}

} // namespace

namespace reaktio::app {

bool AuthoritativeAudioTransport::bind_audio_clip(
    const audio::AudioClipRecord& clip,
    platform::SdlAudioDevice& device,
    foundation::CrashSafeLog& log) {
    transport_ = gameplay::TransportController{clip.duration_seconds};
    if (!clip_source_.bind_clip(clip, device.info().requested_spec, log)) {
        refresh_alignment_diagnostics(0.0);
        return false;
    }

    if (!device.bind_playback_source(clip_source_)) {
        clip_source_.reset();
        refresh_alignment_diagnostics(0.0);
        return false;
    }

    bound_device_ = &device;
    using_audio_authority_ = true;
    update_device_stream_origin();
    sync_from_audio(false);
    alignment_tracker_.reset_to_authoritative(transport_.snapshot());
    pause_or_resume_device_for_current_state();
    refresh_alignment_diagnostics(0.0);
    return true;
}

void AuthoritativeAudioTransport::unbind_audio_clip() noexcept {
    if (bound_device_ != nullptr) {
        bound_device_->pause_playback();
        bound_device_->unbind_playback_source();
        bound_device_ = nullptr;
    }

    clip_source_.reset();
    using_audio_authority_ = false;
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::tick(double simulation_delta_seconds) noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.advance(simulation_delta_seconds);
        refresh_alignment_diagnostics(simulation_delta_seconds);
        return;
    }

    sync_from_audio(true);
    pause_or_resume_device_for_current_state();
    refresh_alignment_diagnostics(simulation_delta_seconds);
}

bool AuthoritativeAudioTransport::using_audio_authority() const noexcept {
    return using_audio_authority_;
}

platform::AudioPlaybackProgress AuthoritativeAudioTransport::playback_progress() const noexcept {
    return bound_device_ != nullptr ? bound_device_->playback_progress() : platform::AudioPlaybackProgress{};
}

TransportDrivenAudioSnapshot AuthoritativeAudioTransport::clip_snapshot() const noexcept {
    return clip_source_.snapshot();
}

const gameplay::TransportSnapshot& AuthoritativeAudioTransport::snapshot() const noexcept {
    return transport_.snapshot();
}

const gameplay::TransportDiagnostics& AuthoritativeAudioTransport::diagnostics() const noexcept {
    return alignment_tracker_.diagnostics();
}

void AuthoritativeAudioTransport::play() noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.play();
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.play();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::pause() noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.pause();
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.pause();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::stop() noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.stop();
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.stop();
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::restart() noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.restart();
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.restart();
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::seek(double position_seconds) noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.seek(position_seconds);
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.seek(position_seconds);
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::preview(double start_seconds, double end_seconds) noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.preview(start_seconds, end_seconds);
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.preview(start_seconds, end_seconds);
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::set_loop_region(double start_seconds, double end_seconds) noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.set_loop_region(start_seconds, end_seconds);
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.set_loop_region(start_seconds, end_seconds);
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::clear_loop_region() noexcept {
    if (!using_audio_authority_ || bound_device_ == nullptr) {
        transport_.clear_loop_region();
        refresh_alignment_diagnostics(0.0);
        return;
    }

    clip_source_.clear_loop_region();
    update_device_stream_origin();
    bound_device_->clear_stream();
    pause_or_resume_device_for_current_state();
    sync_from_audio(false);
    refresh_alignment_diagnostics(0.0);
}

void AuthoritativeAudioTransport::refresh_alignment_diagnostics(double simulation_delta_seconds) noexcept {
    if (using_audio_authority_ && bound_device_ != nullptr) {
        alignment_tracker_.update_from_audio(
            simulation_delta_seconds,
            transport_.snapshot(),
            bound_device_->playback_progress());
        return;
    }

    alignment_tracker_.update_from_simulation(transport_.snapshot());
}

void AuthoritativeAudioTransport::sync_from_audio(bool use_device_position) noexcept {
    TransportDrivenAudioSnapshot clip_playback = clip_source_.snapshot();
    gameplay::TransportSnapshot authoritative_snapshot = clip_playback.transport_snapshot;
    authoritative_snapshot.position_authority = gameplay::TransportPositionAuthority::AudioOutput;

    if (use_device_position && authoritative_snapshot.playback_state == gameplay::TransportPlaybackState::Playing &&
        bound_device_ != nullptr) {
        const platform::AudioPlaybackProgress progress = bound_device_->playback_progress();
        authoritative_snapshot.position_seconds = resolve_authoritative_transport_position(
            clip_playback.transport_snapshot,
            progress);
    }

    transport_.sync_from_authoritative_snapshot(authoritative_snapshot);
}

void AuthoritativeAudioTransport::update_device_stream_origin() noexcept {
    if (bound_device_ == nullptr) {
        return;
    }

    const TransportDrivenAudioSnapshot clip_playback = clip_source_.snapshot();
    bound_device_->set_input_frame_origin(clip_playback.rendered_input_frames);
}

void AuthoritativeAudioTransport::pause_or_resume_device_for_current_state() noexcept {
    if (bound_device_ == nullptr) {
        return;
    }

    const TransportDrivenAudioSnapshot clip_playback = clip_source_.snapshot();
    if (clip_playback.transport_snapshot.playback_state == gameplay::TransportPlaybackState::Playing) {
        bound_device_->resume_playback();
    } else {
        bound_device_->pause_playback();
    }
}

} // namespace reaktio::app