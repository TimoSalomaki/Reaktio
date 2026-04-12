#include "reaktio/platform/SdlAudioDevice.hpp"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <cstddef>
#include <cassert>
#include <cmath>
#include <thread>
#include <utility>
#include <vector>

namespace reaktio::platform {

namespace {

std::string make_sdl_error_message(std::string_view prefix) {
    std::string message(prefix);
    message += SDL_GetError();
    return message;
}

SDL_AudioFormat to_sdl_format(AudioSampleFormat format) noexcept {
    switch (format) {
    case AudioSampleFormat::S16:
        return SDL_AUDIO_S16;
    case AudioSampleFormat::F32:
        return SDL_AUDIO_F32;
    case AudioSampleFormat::Unknown:
        break;
    }

    return SDL_AUDIO_F32;
}

AudioSampleFormat from_sdl_format(SDL_AudioFormat format) noexcept {
    switch (format) {
    case SDL_AUDIO_S16:
        return AudioSampleFormat::S16;
    case SDL_AUDIO_F32:
        return AudioSampleFormat::F32;
    default:
        break;
    }

    return AudioSampleFormat::Unknown;
}

AudioSpec make_audio_spec(const AudioConfig& config) noexcept {
    const SDL_AudioSpec sdl_spec{
        .format = to_sdl_format(config.preferred_format),
        .channels = std::max(config.preferred_channels, 1),
        .freq = std::max(config.preferred_sample_rate, 8000),
    };
    return AudioSpec{
        .format = config.preferred_format,
        .channels = sdl_spec.channels,
        .sample_rate_hz = sdl_spec.freq,
        .bytes_per_frame = static_cast<int>(SDL_AUDIO_FRAMESIZE(sdl_spec)),
    };
}

AudioSpec make_audio_spec(const SDL_AudioSpec& sdl_spec) noexcept {
    return AudioSpec{
        .format = from_sdl_format(sdl_spec.format),
        .channels = sdl_spec.channels,
        .sample_rate_hz = sdl_spec.freq,
        .bytes_per_frame = static_cast<int>(SDL_AUDIO_FRAMESIZE(sdl_spec)),
    };
}

double frames_to_ms(int frames, int sample_rate_hz) noexcept {
    if (frames <= 0 || sample_rate_hz <= 0) {
        return 0.0;
    }

    return static_cast<double>(frames) * 1000.0 / static_cast<double>(sample_rate_hz);
}

int bytes_to_frames(int byte_count, const AudioSpec& spec) noexcept {
    if (byte_count <= 0 || spec.bytes_per_frame <= 0) {
        return 0;
    }

    return byte_count / spec.bytes_per_frame;
}

} // namespace

struct SdlAudioDevice::Impl {
    explicit Impl(AudioConfig config_value)
        : config(std::move(config_value)),
          main_thread_id(std::this_thread::get_id()) {
        info.requested_spec = make_audio_spec(config);
    }

    ~Impl() {
        shutdown();
    }

    void assert_main_thread() const noexcept {
        assert(std::this_thread::get_id() == main_thread_id);
    }

    static void SDLCALL request_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
        auto* impl = static_cast<Impl*>(userdata);
        if (impl == nullptr || stream == nullptr || impl->playback_source == nullptr || impl->info.requested_spec.bytes_per_frame <= 0) {
            return;
        }

        const int requested_bytes = std::max(additional_amount, total_amount);
        if (requested_bytes <= 0) {
            return;
        }

        const int requested_frames = std::max(
            1,
            (requested_bytes + impl->info.requested_spec.bytes_per_frame - 1) /
                impl->info.requested_spec.bytes_per_frame);
        const std::size_t required_bytes = static_cast<std::size_t>(requested_frames) *
            static_cast<std::size_t>(impl->info.requested_spec.bytes_per_frame);
        if (impl->callback_scratch.size() < required_bytes) {
            impl->callback_scratch.resize(required_bytes);
        }

        const int generated_frames = impl->playback_source->render_audio_frames(
            impl->callback_scratch.data(),
            requested_frames,
            impl->info.requested_spec);
        if (generated_frames <= 0) {
            return;
        }

        const int generated_bytes = generated_frames * impl->info.requested_spec.bytes_per_frame;
        if (!SDL_PutAudioStreamData(stream, impl->callback_scratch.data(), generated_bytes)) {
            return;
        }

        impl->submitted_input_frames += static_cast<std::uint64_t>(generated_frames);
    }

    bool initialize() {
        assert_main_thread();

        info.requested_spec = make_audio_spec(config);
        if (!config.enable_playback_device) {
            info.state = AudioDeviceOpenState::Disabled;
            info.status_message = "playback device disabled by configuration";
            return true;
        }

        const bool had_audio_subsystem = (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u;
        if (!had_audio_subsystem && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            info.state = AudioDeviceOpenState::Unavailable;
            info.status_message = make_sdl_error_message("SDL_InitSubSystem(SDL_INIT_AUDIO) failed: ");
            return false;
        }

        owns_audio_subsystem = !had_audio_subsystem;

        const SDL_AudioSpec desired_spec{
            .format = to_sdl_format(config.preferred_format),
            .channels = std::max(config.preferred_channels, 1),
            .freq = std::max(config.preferred_sample_rate, 8000),
        };

        std::string previous_sample_frames_hint;
        const char* existing_sample_frames_hint = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
        const bool had_sample_frames_hint = existing_sample_frames_hint != nullptr;
        if (had_sample_frames_hint) {
            previous_sample_frames_hint = existing_sample_frames_hint;
        }

        if (config.preferred_buffer_frames > 0) {
            const std::string preferred_sample_frames_hint = std::to_string(config.preferred_buffer_frames);
            SDL_SetHint(
                SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,
                preferred_sample_frames_hint.c_str());
        }

        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired_spec, nullptr, nullptr);
        if (had_sample_frames_hint) {
            SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, previous_sample_frames_hint.c_str());
        } else {
            SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
        }

        if (stream == nullptr) {
            info.state = AudioDeviceOpenState::Unavailable;
            info.status_message = make_sdl_error_message("SDL_OpenAudioDeviceStream failed: ");
            cleanup_audio_subsystem();
            return false;
        }

        logical_device_id = SDL_GetAudioStreamDevice(stream);
        if (logical_device_id == 0u) {
            info.state = AudioDeviceOpenState::Unavailable;
            info.status_message = make_sdl_error_message("SDL_GetAudioStreamDevice failed: ");
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            cleanup_audio_subsystem();
            return false;
        }

        if (!config.start_paused && !SDL_ResumeAudioStreamDevice(stream)) {
            info.state = AudioDeviceOpenState::Unavailable;
            info.status_message = make_sdl_error_message("SDL_ResumeAudioStreamDevice failed: ");
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            logical_device_id = 0u;
            cleanup_audio_subsystem();
            return false;
        }

        if (!std::isfinite(config.device_gain) || config.device_gain < 0.0f) {
            config.device_gain = 1.0f;
        }

        std::string open_status{"opened playback audio device"};
        if (!SDL_SetAudioDeviceGain(logical_device_id, config.device_gain)) {
            open_status += "; SDL_SetAudioDeviceGain failed";
        }

        refresh_info(open_status);
        return true;
    }

    void refresh_info(std::string_view status_message) noexcept {
        info.state = AudioDeviceOpenState::Opened;
        info.playback = true;
        info.using_default_device = true;
        info.logical_device_id = logical_device_id;
        info.paused = stream != nullptr ? SDL_AudioStreamDevicePaused(stream) : true;
        info.driver_name = SDL_GetCurrentAudioDriver() != nullptr ? SDL_GetCurrentAudioDriver() : "<none>";
        info.device_name = SDL_GetAudioDeviceName(logical_device_id) != nullptr
            ? SDL_GetAudioDeviceName(logical_device_id)
            : "<unknown>";
        info.status_message = std::string(status_message);

        SDL_AudioSpec actual_spec{};
        int sample_frames = 0;
        if (SDL_GetAudioDeviceFormat(logical_device_id, &actual_spec, &sample_frames)) {
            info.actual_spec = make_audio_spec(actual_spec);
            info.latency.device_buffer_frames = sample_frames;
            info.latency.device_period_ms = frames_to_ms(sample_frames, actual_spec.freq);
            info.latency.total_output_latency_ms = info.latency.device_period_ms;
            info.latency.query_mode = AudioLatencyQueryMode::DevicePeriodOnly;
        } else {
            info.actual_spec = info.requested_spec;
            info.status_message += "; SDL_GetAudioDeviceFormat failed";
        }

        const float queried_gain = SDL_GetAudioDeviceGain(logical_device_id);
        info.gain = queried_gain >= 0.0f ? queried_gain : config.device_gain;

        if (stream != nullptr) {
            const int queued_bytes = SDL_GetAudioStreamQueued(stream);
            if (queued_bytes >= 0) {
                info.latency.queued_stream_frames = bytes_to_frames(queued_bytes, info.actual_spec);
                info.latency.queued_stream_latency_ms = frames_to_ms(
                    info.latency.queued_stream_frames,
                    info.actual_spec.sample_rate_hz);
                info.latency.total_output_latency_ms =
                    info.latency.device_period_ms + info.latency.queued_stream_latency_ms;
                info.latency.query_mode = AudioLatencyQueryMode::DevicePeriodPlusQueuedStream;
            }
        }
    }

    void shutdown() noexcept {
        assert_main_thread();

        if (stream != nullptr) {
            SDL_SetAudioStreamGetCallback(stream, nullptr, nullptr);
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }

        playback_source = nullptr;
        callback_scratch.clear();
        submitted_input_frames = 0;
        submitted_input_frame_origin = 0;
        logical_device_id = 0u;
        cleanup_audio_subsystem();
    }

    bool bind_playback_source(IAudioStreamSource& source) noexcept {
        assert_main_thread();
        if (stream == nullptr) {
            return false;
        }

        if (!SDL_LockAudioStream(stream)) {
            return false;
        }

        playback_source = &source;
        submitted_input_frames = 0;
        submitted_input_frame_origin = 0;
        const bool cleared = SDL_ClearAudioStream(stream);
        const bool callback_set = SDL_SetAudioStreamGetCallback(stream, request_callback, this);
        const bool unlocked = SDL_UnlockAudioStream(stream);
        (void)unlocked;
        return cleared && callback_set;
    }

    void unbind_playback_source() noexcept {
        assert_main_thread();
        if (stream == nullptr) {
            playback_source = nullptr;
            submitted_input_frames = 0;
            submitted_input_frame_origin = 0;
            callback_scratch.clear();
            return;
        }

        if (!SDL_LockAudioStream(stream)) {
            return;
        }

        SDL_SetAudioStreamGetCallback(stream, nullptr, nullptr);
        SDL_ClearAudioStream(stream);
        playback_source = nullptr;
        submitted_input_frames = 0;
        submitted_input_frame_origin = 0;
        callback_scratch.clear();
        SDL_UnlockAudioStream(stream);
    }

    bool clear_stream() noexcept {
        assert_main_thread();
        if (stream == nullptr) {
            return false;
        }

        if (!SDL_LockAudioStream(stream)) {
            return false;
        }

        submitted_input_frames = 0;
        const bool cleared = SDL_ClearAudioStream(stream);
        const bool unlocked = SDL_UnlockAudioStream(stream);
        (void)unlocked;
        return cleared;
    }

    void set_input_frame_origin(std::uint64_t frame_index) noexcept {
        assert_main_thread();
        submitted_input_frame_origin = frame_index;
    }

    bool pause_playback() noexcept {
        assert_main_thread();
        return stream != nullptr && SDL_PauseAudioStreamDevice(stream);
    }

    bool resume_playback() noexcept {
        assert_main_thread();
        return stream != nullptr && SDL_ResumeAudioStreamDevice(stream);
    }

    AudioPlaybackProgress playback_progress() const noexcept {
        AudioPlaybackProgress progress{};
        const AudioSpec& input_spec = info.requested_spec;
        if (stream == nullptr || input_spec.sample_rate_hz <= 0) {
            return progress;
        }

        if (!SDL_LockAudioStream(stream)) {
            return progress;
        }

        progress.source_bound = playback_source != nullptr;
        progress.device_paused = SDL_AudioStreamDevicePaused(stream);
        const std::uint64_t frame_origin = submitted_input_frame_origin;
        const std::uint64_t submitted_frames = submitted_input_frames;
        const int queued_bytes = SDL_GetAudioStreamQueued(stream);
        const bool unlocked = SDL_UnlockAudioStream(stream);
        (void)unlocked;

        const std::uint64_t queued_frames = queued_bytes > 0
            ? static_cast<std::uint64_t>(bytes_to_frames(queued_bytes, input_spec))
            : 0ull;
        const std::uint64_t consumed_frames = submitted_frames > queued_frames
            ? submitted_frames - queued_frames
            : 0ull;
        const std::uint64_t absolute_submitted_frames = frame_origin + submitted_frames;
        const std::uint64_t absolute_consumed_frames = frame_origin + consumed_frames;

        progress.submitted_input_frames = absolute_submitted_frames;
        progress.queued_input_frames = queued_frames;
        progress.consumed_input_frames = absolute_consumed_frames;
        progress.stream_consumed_seconds =
            static_cast<double>(absolute_consumed_frames) / static_cast<double>(input_spec.sample_rate_hz);
        progress.queued_input_seconds =
            static_cast<double>(queued_frames) / static_cast<double>(input_spec.sample_rate_hz);
        progress.device_latency_seconds = info.latency.device_period_ms / 1000.0;
        progress.total_output_latency_seconds =
            progress.device_latency_seconds + progress.queued_input_seconds;
        progress.authoritative_position_seconds = std::max(
            0.0,
            progress.stream_consumed_seconds - progress.device_latency_seconds);
        progress.authoritative_position_mode = AudioPlaybackPositionMode::OutputLatencyCompensated;
        return progress;
    }

    void cleanup_audio_subsystem() noexcept {
        if (owns_audio_subsystem && (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        owns_audio_subsystem = false;
    }

    AudioConfig config;
    AudioDeviceInfo info;
    std::thread::id main_thread_id;
    SDL_AudioStream* stream{};
    SDL_AudioDeviceID logical_device_id{};
    IAudioStreamSource* playback_source{};
    std::vector<std::byte> callback_scratch;
    std::uint64_t submitted_input_frames{};
    std::uint64_t submitted_input_frame_origin{};
    bool owns_audio_subsystem{};
};

SdlAudioDevice::SdlAudioDevice(AudioConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

SdlAudioDevice::~SdlAudioDevice() = default;

bool SdlAudioDevice::initialize() {
    return impl_->initialize();
}

void SdlAudioDevice::shutdown() noexcept {
    impl_->shutdown();
}

bool SdlAudioDevice::bind_playback_source(IAudioStreamSource& source) noexcept {
    return impl_->bind_playback_source(source);
}

void SdlAudioDevice::unbind_playback_source() noexcept {
    impl_->unbind_playback_source();
}

bool SdlAudioDevice::clear_stream() noexcept {
    return impl_->clear_stream();
}

void SdlAudioDevice::set_input_frame_origin(std::uint64_t frame_index) noexcept {
    impl_->set_input_frame_origin(frame_index);
}

bool SdlAudioDevice::pause_playback() noexcept {
    return impl_->pause_playback();
}

bool SdlAudioDevice::resume_playback() noexcept {
    return impl_->resume_playback();
}

const AudioConfig& SdlAudioDevice::config() const noexcept {
    return impl_->config;
}

const AudioDeviceInfo& SdlAudioDevice::info() const noexcept {
    return impl_->info;
}

AudioPlaybackProgress SdlAudioDevice::playback_progress() const noexcept {
    return impl_->playback_progress();
}

} // namespace reaktio::platform