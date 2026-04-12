#include "reaktio/app/TransportDrivenAudioSource.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

namespace reaktio::app {

namespace {

struct SdlBuffer {
    Uint8* bytes{};

    ~SdlBuffer() {
        if (bytes != nullptr) {
            SDL_free(bytes);
        }
    }
};

SDL_AudioFormat to_sdl_format(platform::AudioSampleFormat format) noexcept {
    switch (format) {
    case platform::AudioSampleFormat::S16:
        return SDL_AUDIO_S16;
    case platform::AudioSampleFormat::F32:
        return SDL_AUDIO_F32;
    case platform::AudioSampleFormat::Unknown:
        break;
    }

    return SDL_AUDIO_F32;
}

void log_decode_error(
    foundation::CrashSafeLog& log,
    const std::filesystem::path& source_path,
    std::string_view message) {
    std::string formatted(message);
    formatted += " [" + source_path.string() + "]";
    log.write(foundation::LogLevel::Error, formatted);
}

} // namespace

bool TransportDrivenAudioSource::bind_clip(
    const audio::AudioClipRecord& clip,
    const platform::AudioSpec& input_spec,
    foundation::CrashSafeLog& log) {
    if (clip.interleaved_samples.empty() || input_spec.sample_rate_hz <= 0 || input_spec.channels <= 0 ||
        input_spec.bytes_per_frame <= 0 || input_spec.format == platform::AudioSampleFormat::Unknown) {
        log_decode_error(log, clip.source_path, "Unable to bind audio clip to playback source due to an invalid clip or input spec.");
        reset();
        return false;
    }

    const SDL_AudioSpec source_spec{
        .format = SDL_AUDIO_F32,
        .channels = clip.channel_count,
        .freq = static_cast<int>(clip.sample_rate_hz),
    };
    const SDL_AudioSpec target_spec{
        .format = to_sdl_format(input_spec.format),
        .channels = input_spec.channels,
        .freq = input_spec.sample_rate_hz,
    };

    SdlBuffer converted_buffer{};
    int converted_length = 0;
    if (!SDL_ConvertAudioSamples(
            &source_spec,
            reinterpret_cast<const Uint8*>(clip.interleaved_samples.data()),
            static_cast<int>(clip.interleaved_samples.size() * sizeof(float)),
            &target_spec,
            &converted_buffer.bytes,
            &converted_length)) {
        log_decode_error(log, clip.source_path, "Unable to convert audio clip into the playback stream input format.");
        reset();
        return false;
    }

    if (converted_length <= 0 || (converted_length % input_spec.bytes_per_frame) != 0) {
        log_decode_error(log, clip.source_path, "Converted audio clip produced an invalid frame layout for playback.");
        reset();
        return false;
    }

    std::scoped_lock guard(mutex_);
    input_spec_ = input_spec;
    converted_bytes_.resize(static_cast<std::size_t>(converted_length));
    std::memcpy(converted_bytes_.data(), converted_buffer.bytes, converted_bytes_.size());
    total_frames_ = static_cast<std::uint64_t>(converted_length / input_spec.bytes_per_frame);
    rendered_frames_ = 0;
    transport_snapshot_ = gameplay::TransportSnapshot{};
    transport_snapshot_.position_authority = gameplay::TransportPositionAuthority::AudioOutput;
    transport_snapshot_.duration_seconds = static_cast<double>(total_frames_) / static_cast<double>(input_spec.sample_rate_hz);
    transport_snapshot_.playback_rate = 1.0;
    transport_snapshot_.position_seconds = 0.0;
    ready_ = true;
    finished_ = true;
    return true;
}

void TransportDrivenAudioSource::play() noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double previous_position = transport_snapshot_.position_seconds;
    if (transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot_.preview_region.enabled) {
        set_rendered_frame(std::clamp(rendered_frames_, current_preview_start_frame(), current_preview_end_frame()));
    } else if (rendered_frames_ >= total_frames_) {
        const std::uint64_t restart_frame = transport_snapshot_.loop_region.enabled
            ? current_loop_start_frame()
            : 0u;
        set_rendered_frame(restart_frame);
        if (position_changed(previous_position, transport_snapshot_.position_seconds)) {
            record_discontinuity(
                gameplay::TransportDiscontinuityReason::Restart,
                previous_position,
                transport_snapshot_.position_seconds);
        }
    }

    transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Playing;
    finished_ = false;
}

void TransportDrivenAudioSource::pause() noexcept {
    std::scoped_lock guard(mutex_);
    if (transport_snapshot_.playback_state == gameplay::TransportPlaybackState::Playing) {
        transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Paused;
    }
}

void TransportDrivenAudioSource::reset() noexcept {
    std::scoped_lock guard(mutex_);
    input_spec_ = {};
    converted_bytes_.clear();
    total_frames_ = 0;
    rendered_frames_ = 0;
    transport_snapshot_ = gameplay::TransportSnapshot{};
    transport_snapshot_.position_authority = gameplay::TransportPositionAuthority::AudioOutput;
    ready_ = false;
    finished_ = true;
}

void TransportDrivenAudioSource::stop() noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double previous_position = transport_snapshot_.position_seconds;
    const gameplay::TransportPlaybackState previous_state = transport_snapshot_.playback_state;
    const bool was_preview = transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview;
    transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Stopped;
    set_rendered_frame(0u);
    clear_preview_region();
    finished_ = false;
    if (was_preview || previous_state != gameplay::TransportPlaybackState::Stopped || position_changed(previous_position, 0.0)) {
        record_discontinuity(
            gameplay::TransportDiscontinuityReason::Stop,
            previous_position,
            transport_snapshot_.position_seconds);
    }
}

void TransportDrivenAudioSource::restart() noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double previous_position = transport_snapshot_.position_seconds;
    if (transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot_.preview_region.enabled) {
        set_rendered_frame(current_preview_start_frame());
    } else if (transport_snapshot_.loop_region.enabled) {
        set_rendered_frame(current_loop_start_frame());
    } else {
        set_rendered_frame(0u);
    }

    transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Playing;
    finished_ = false;
    record_discontinuity(
        gameplay::TransportDiscontinuityReason::Restart,
        previous_position,
        transport_snapshot_.position_seconds);
}

void TransportDrivenAudioSource::seek(double position_seconds) noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double previous_position = transport_snapshot_.position_seconds;
    std::uint64_t target_frame = frame_from_seconds(position_seconds);
    if (transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot_.preview_region.enabled) {
        target_frame = std::clamp(target_frame, current_preview_start_frame(), current_preview_end_frame());
    }

    set_rendered_frame(target_frame);
    finished_ = transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot_.preview_region.enabled
        ? rendered_frames_ >= current_preview_end_frame()
        : rendered_frames_ >= total_frames_;
    if (position_changed(previous_position, transport_snapshot_.position_seconds)) {
        record_discontinuity(
            gameplay::TransportDiscontinuityReason::Seek,
            previous_position,
            transport_snapshot_.position_seconds);
    }
}

void TransportDrivenAudioSource::preview(double start_seconds, double end_seconds) noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double clamped_start = std::clamp(start_seconds, 0.0, transport_snapshot_.duration_seconds);
    const double clamped_end = std::clamp(end_seconds, 0.0, transport_snapshot_.duration_seconds);
    if (clamped_end - clamped_start < 0.001) {
        return;
    }

    const double previous_position = transport_snapshot_.position_seconds;
    transport_snapshot_.preview_region = gameplay::TransportPreviewRegion{
        .start_seconds = clamped_start,
        .end_seconds = clamped_end,
        .enabled = true,
    };
    transport_snapshot_.playback_mode = gameplay::TransportPlaybackMode::Preview;
    set_rendered_frame(current_preview_start_frame());
    transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Playing;
    finished_ = false;
    record_discontinuity(
        gameplay::TransportDiscontinuityReason::PreviewStart,
        previous_position,
        transport_snapshot_.position_seconds);
}

void TransportDrivenAudioSource::set_loop_region(double start_seconds, double end_seconds) noexcept {
    std::scoped_lock guard(mutex_);
    if (!ready_) {
        return;
    }

    const double clamped_start = std::clamp(start_seconds, 0.0, transport_snapshot_.duration_seconds);
    const double clamped_end = std::clamp(end_seconds, 0.0, transport_snapshot_.duration_seconds);
    if (clamped_end - clamped_start < 0.001) {
        transport_snapshot_.loop_region = gameplay::TransportLoopRegion{};
        return;
    }

    transport_snapshot_.loop_region = gameplay::TransportLoopRegion{
        .start_seconds = clamped_start,
        .end_seconds = clamped_end,
        .enabled = true,
    };

    if (transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Normal &&
        (transport_snapshot_.position_seconds < transport_snapshot_.loop_region.start_seconds ||
         transport_snapshot_.position_seconds > transport_snapshot_.loop_region.end_seconds)) {
        const double previous_position = transport_snapshot_.position_seconds;
        set_rendered_frame(current_loop_start_frame());
        finished_ = false;
        record_discontinuity(
            gameplay::TransportDiscontinuityReason::LoopRegionClamp,
            previous_position,
            transport_snapshot_.position_seconds);
    }
}

void TransportDrivenAudioSource::clear_loop_region() noexcept {
    std::scoped_lock guard(mutex_);
    transport_snapshot_.loop_region = gameplay::TransportLoopRegion{};
}

TransportDrivenAudioSnapshot TransportDrivenAudioSource::snapshot() const noexcept {
    std::scoped_lock guard(mutex_);
    return TransportDrivenAudioSnapshot{
        .ready = ready_,
        .finished = finished_,
        .total_frames = total_frames_,
        .rendered_input_frames = rendered_frames_,
        .transport_snapshot = transport_snapshot_,
    };
}

int TransportDrivenAudioSource::render_audio_frames(
    std::byte* destination,
    int requested_frames,
    const platform::AudioSpec& input_spec) noexcept {
    if (destination == nullptr || requested_frames <= 0) {
        return 0;
    }

    std::scoped_lock guard(mutex_);
    if (!ready_ || finished_ || input_spec.bytes_per_frame <= 0 ||
        input_spec.bytes_per_frame != input_spec_.bytes_per_frame ||
        input_spec.sample_rate_hz != input_spec_.sample_rate_hz ||
        input_spec.channels != input_spec_.channels ||
        input_spec.format != input_spec_.format ||
        transport_snapshot_.playback_state != gameplay::TransportPlaybackState::Playing) {
        return 0;
    }

    int rendered_frame_count = 0;
    while (rendered_frame_count < requested_frames &&
           transport_snapshot_.playback_state == gameplay::TransportPlaybackState::Playing) {
        const bool preview_active = transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
            transport_snapshot_.preview_region.enabled;
        const bool loop_active = !preview_active && transport_snapshot_.loop_region.enabled;
        const std::uint64_t active_end_frame = preview_active
            ? current_preview_end_frame()
            : (loop_active ? current_loop_end_frame() : total_frames_);

        if (rendered_frames_ >= active_end_frame) {
            handle_render_boundary();
            continue;
        }

        const std::uint64_t available_frames = active_end_frame - rendered_frames_;
        const int chunk_frames = static_cast<int>(std::min<std::uint64_t>(
            available_frames,
            static_cast<std::uint64_t>(requested_frames - rendered_frame_count)));
        if (chunk_frames <= 0) {
            break;
        }

        const std::size_t byte_offset = static_cast<std::size_t>(rendered_frames_) *
            static_cast<std::size_t>(input_spec_.bytes_per_frame);
        const std::size_t byte_count = static_cast<std::size_t>(chunk_frames) *
            static_cast<std::size_t>(input_spec_.bytes_per_frame);
        std::memcpy(
            destination + static_cast<std::size_t>(rendered_frame_count) * static_cast<std::size_t>(input_spec_.bytes_per_frame),
            converted_bytes_.data() + byte_offset,
            byte_count);

        rendered_frames_ += static_cast<std::uint64_t>(chunk_frames);
        transport_snapshot_.position_seconds = seconds_from_frame(rendered_frames_);
        rendered_frame_count += chunk_frames;

        if (rendered_frames_ >= active_end_frame) {
            handle_render_boundary();
        }
    }

    return rendered_frame_count;
}

bool TransportDrivenAudioSource::position_changed(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) > 0.000001;
}

double TransportDrivenAudioSource::seconds_from_frame(std::uint64_t frame_index) const noexcept {
    if (input_spec_.sample_rate_hz <= 0) {
        return 0.0;
    }

    return static_cast<double>(frame_index) / static_cast<double>(input_spec_.sample_rate_hz);
}

std::uint64_t TransportDrivenAudioSource::frame_from_seconds(double seconds) const noexcept {
    const double clamped = std::clamp(seconds, 0.0, transport_snapshot_.duration_seconds);
    return static_cast<std::uint64_t>(
        std::llround(clamped * static_cast<double>(std::max(input_spec_.sample_rate_hz, 1))));
}

std::uint64_t TransportDrivenAudioSource::current_loop_start_frame() const noexcept {
    return frame_from_seconds(transport_snapshot_.loop_region.start_seconds);
}

std::uint64_t TransportDrivenAudioSource::current_loop_end_frame() const noexcept {
    return frame_from_seconds(transport_snapshot_.loop_region.end_seconds);
}

std::uint64_t TransportDrivenAudioSource::current_preview_start_frame() const noexcept {
    return frame_from_seconds(transport_snapshot_.preview_region.start_seconds);
}

std::uint64_t TransportDrivenAudioSource::current_preview_end_frame() const noexcept {
    return frame_from_seconds(transport_snapshot_.preview_region.end_seconds);
}

void TransportDrivenAudioSource::set_rendered_frame(std::uint64_t frame_index) noexcept {
    rendered_frames_ = std::min(frame_index, total_frames_);
    transport_snapshot_.position_seconds = seconds_from_frame(rendered_frames_);
}

void TransportDrivenAudioSource::clear_preview_region() noexcept {
    transport_snapshot_.preview_region = gameplay::TransportPreviewRegion{};
    transport_snapshot_.playback_mode = gameplay::TransportPlaybackMode::Normal;
}

void TransportDrivenAudioSource::record_discontinuity(
    gameplay::TransportDiscontinuityReason reason,
    double from_seconds,
    double to_seconds) noexcept {
    ++transport_snapshot_.discontinuity.timeline_revision;
    ++transport_snapshot_.discontinuity.discontinuity_count;
    transport_snapshot_.discontinuity.last_reason = reason;
    transport_snapshot_.discontinuity.last_from_seconds = from_seconds;
    transport_snapshot_.discontinuity.last_to_seconds = to_seconds;
}

void TransportDrivenAudioSource::handle_render_boundary() noexcept {
    if (transport_snapshot_.playback_mode == gameplay::TransportPlaybackMode::Preview &&
        transport_snapshot_.preview_region.enabled &&
        rendered_frames_ >= current_preview_end_frame()) {
        const double previous_position = transport_snapshot_.position_seconds;
        set_rendered_frame(current_preview_end_frame());
        transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Stopped;
        ++transport_snapshot_.completed_previews;
        record_discontinuity(
            gameplay::TransportDiscontinuityReason::PreviewComplete,
            previous_position,
            transport_snapshot_.position_seconds);
        clear_preview_region();
        finished_ = true;
        return;
    }

    if (transport_snapshot_.loop_region.enabled && rendered_frames_ >= current_loop_end_frame()) {
        const double previous_position = transport_snapshot_.position_seconds;
        set_rendered_frame(current_loop_start_frame());
        ++transport_snapshot_.completed_loops;
        record_discontinuity(
            gameplay::TransportDiscontinuityReason::LoopWrap,
            previous_position,
            transport_snapshot_.position_seconds);
        finished_ = false;
        return;
    }

    if (rendered_frames_ >= total_frames_) {
        set_rendered_frame(total_frames_);
        transport_snapshot_.playback_state = gameplay::TransportPlaybackState::Stopped;
        finished_ = true;
    }
}

} // namespace reaktio::app