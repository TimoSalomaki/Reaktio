#include "reaktio/platform/SdlApplicationShell.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <cassert>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace reaktio::platform {

namespace {

std::string make_sdl_error_message(std::string_view prefix) {
    std::string message(prefix);
    message += SDL_GetError();
    return message;
}

} // namespace

struct SdlApplicationShell::Impl {
    explicit Impl(ApplicationConfig config_value, foundation::CrashSafeLog& sink)
        : config(std::move(config_value)),
          log(&sink),
          main_thread_id(std::this_thread::get_id()),
          frame_clock(config.main_loop) {}

    ~Impl() {
        shutdown();
    }

    void assert_main_thread() const noexcept {
        assert(std::this_thread::get_id() == main_thread_id);
    }

    bool initialize() {
        assert_main_thread();

        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            log->write(foundation::LogLevel::Error, make_sdl_error_message("SDL_Init failed: "));
            return false;
        }

        initialized = true;

        if (!create_window()) {
            shutdown();
            return false;
        }

        if (config.window.enable_text_input && !SDL_StartTextInput(window)) {
            log->write(
                foundation::LogLevel::Warning,
                make_sdl_error_message("SDL_StartTextInput failed: "));
        }

        refresh_window_state();
        return true;
    }

    void shutdown() noexcept {
        assert_main_thread();

        for (auto& [instance_id, gamepad] : opened_gamepads) {
            (void)instance_id;
            SDL_CloseGamepad(gamepad);
        }
        opened_gamepads.clear();

        if (window != nullptr) {
            if (config.window.enable_text_input) {
                SDL_StopTextInput(window);
            }

            SDL_DestroyWindow(window);
            window = nullptr;
        }

        if (initialized) {
            SDL_Quit();
            initialized = false;
        }
    }

    bool create_window() {
        SDL_WindowFlags flags = 0;
        if (config.window.start_hidden) {
            flags |= SDL_WINDOW_HIDDEN;
        }
        if (config.window.resizable) {
            flags |= SDL_WINDOW_RESIZABLE;
        }
        if (config.window.high_pixel_density) {
            flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        if (config.window.mode == WindowMode::BorderlessWindowed) {
            flags |= SDL_WINDOW_BORDERLESS;
        }
        if (config.window.mode == WindowMode::Fullscreen) {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        window = SDL_CreateWindow(config.window.title.c_str(), config.window.width, config.window.height, flags);
        if (window == nullptr) {
            log->write(foundation::LogLevel::Error, make_sdl_error_message("SDL_CreateWindow failed: "));
            return false;
        }

        if (!config.window.start_hidden && !SDL_ShowWindow(window)) {
            log->write(foundation::LogLevel::Warning, make_sdl_error_message("SDL_ShowWindow failed: "));
        }

        if (config.window.mode == WindowMode::BorderlessWindowed) {
            windowed_mode_before_fullscreen = WindowMode::BorderlessWindowed;
        }

        return true;
    }

    void begin_frame() {
        assert_main_thread();
        input_snapshot.begin_frame();
        frame_clock.begin_frame();
        refresh_window_state();
    }

    void pump_events() {
        assert_main_thread();

        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            handle_event(event);
        }
    }

    void present() const noexcept {}

    void sleep_until_next_fixed_step() const noexcept {
        const std::uint64_t wait_ns = frame_clock.ns_until_next_fixed_step();
        if (wait_ns > 0) {
            SDL_DelayNS(wait_ns);
        }
    }

    void request_quit() noexcept {
        quit_requested = true;
    }

    bool apply_window_mode(WindowMode mode) {
        assert_main_thread();

        if (window == nullptr) {
            return false;
        }

        if (mode != WindowMode::Fullscreen) {
            windowed_mode_before_fullscreen = mode;
        }

        if (!SDL_SetWindowFullscreen(window, mode == WindowMode::Fullscreen)) {
            log->write(
                foundation::LogLevel::Warning,
                make_sdl_error_message("SDL_SetWindowFullscreen failed: "));
            return false;
        }

        if (!SDL_SetWindowBordered(window, mode != WindowMode::BorderlessWindowed)) {
            log->write(
                foundation::LogLevel::Warning,
                make_sdl_error_message("SDL_SetWindowBordered failed: "));
            return false;
        }

        refresh_window_state();
        return true;
    }

    void refresh_window_state() {
        if (window == nullptr) {
            return;
        }

        int logical_width = 0;
        int logical_height = 0;
        int pixel_width = 0;
        int pixel_height = 0;
        (void)SDL_GetWindowSize(window, &logical_width, &logical_height);
        (void)SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height);

        const SDL_WindowFlags flags = SDL_GetWindowFlags(window);

        window_state.id = SDL_GetWindowID(window);
        window_state.logical_width = logical_width;
        window_state.logical_height = logical_height;
        window_state.pixel_width = pixel_width;
        window_state.pixel_height = pixel_height;
        window_state.display_scale = SDL_GetWindowDisplayScale(window);
        window_state.resizable = (flags & SDL_WINDOW_RESIZABLE) != 0;
        window_state.visible = (flags & SDL_WINDOW_HIDDEN) == 0;
        window_state.input_focus = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
        window_state.mode = (flags & SDL_WINDOW_FULLSCREEN) != 0
            ? WindowMode::Fullscreen
            : ((flags & SDL_WINDOW_BORDERLESS) != 0 ? WindowMode::BorderlessWindowed : WindowMode::Windowed);
        window_state.native_handle = query_native_window_handle();
    }

    NativeWindowHandle query_native_window_handle() const {
        NativeWindowHandle handle{};
        if (window == nullptr) {
            return handle;
        }

        const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        if (properties == 0) {
            return handle;
        }

#if defined(_WIN32)
        handle.platform = NativeWindowPlatform::Win32;
        handle.primary_handle = reinterpret_cast<std::uintptr_t>(
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#elif defined(__APPLE__)
        handle.platform = NativeWindowPlatform::Cocoa;
        handle.primary_handle = reinterpret_cast<std::uintptr_t>(
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
#elif defined(__linux__)
        const void* wayland_surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        if (wayland_surface != nullptr) {
            handle.platform = NativeWindowPlatform::Wayland;
            handle.display_connection = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            handle.primary_handle = reinterpret_cast<std::uintptr_t>(wayland_surface);
            return handle;
        }

        const std::int64_t x11_window = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        if (x11_window != 0) {
            handle.platform = NativeWindowPlatform::X11;
            handle.display_connection = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            handle.primary_handle = static_cast<std::uintptr_t>(x11_window);
        }
#endif

        return handle;
    }

    void handle_event(const SDL_Event& event) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            request_quit();
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == window_state.id) {
                window_state.close_requested = true;
                request_quit();
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            refresh_window_state();
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            input_snapshot.record_keyboard_event(KeyboardEvent{
                .timestamp_ns = event.key.timestamp,
                .window_id = event.key.windowID,
                .scancode = static_cast<std::int32_t>(event.key.scancode),
                .keycode = static_cast<std::int32_t>(event.key.key),
                .modifiers = static_cast<std::uint16_t>(event.key.mod),
                .down = event.key.down,
                .repeat = event.key.repeat,
            });
            break;
        case SDL_EVENT_TEXT_EDITING:
            input_snapshot.record_text_editing_event(TextEditingEvent{
                .timestamp_ns = event.edit.timestamp,
                .window_id = event.edit.windowID,
                .text = event.edit.text != nullptr ? event.edit.text : "",
                .start = event.edit.start,
                .length = event.edit.length,
            });
            break;
        case SDL_EVENT_TEXT_EDITING_CANDIDATES: {
            TextEditingCandidatesEvent candidates_event{};
            candidates_event.timestamp_ns = event.edit_candidates.timestamp;
            candidates_event.window_id = event.edit_candidates.windowID;
            candidates_event.selected_candidate = event.edit_candidates.selected_candidate;
            candidates_event.horizontal = event.edit_candidates.horizontal;

            for (std::int32_t index = 0; index < event.edit_candidates.num_candidates; ++index) {
                const char* candidate = event.edit_candidates.candidates[index];
                candidates_event.candidates.emplace_back(candidate != nullptr ? candidate : "");
            }

            input_snapshot.record_text_editing_candidates_event(std::move(candidates_event));
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            input_snapshot.record_text_input_event(TextInputEvent{
                .timestamp_ns = event.text.timestamp,
                .window_id = event.text.windowID,
                .text = event.text.text != nullptr ? event.text.text : "",
            });
            break;
        case SDL_EVENT_MOUSE_MOTION:
            input_snapshot.record_mouse_motion_event(MouseMotionEvent{
                .timestamp_ns = event.motion.timestamp,
                .window_id = event.motion.windowID,
                .state_mask = event.motion.state,
                .x = event.motion.x,
                .y = event.motion.y,
                .delta_x = event.motion.xrel,
                .delta_y = event.motion.yrel,
            });
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            input_snapshot.record_mouse_button_event(MouseButtonEvent{
                .timestamp_ns = event.button.timestamp,
                .window_id = event.button.windowID,
                .button = event.button.button,
                .down = event.button.down,
                .clicks = event.button.clicks,
                .x = event.button.x,
                .y = event.button.y,
            });
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input_snapshot.record_mouse_wheel_event(MouseWheelEvent{
                .timestamp_ns = event.wheel.timestamp,
                .window_id = event.wheel.windowID,
                .x = event.wheel.x,
                .y = event.wheel.y,
                .ticks_x = event.wheel.integer_x,
                .ticks_y = event.wheel.integer_y,
            });
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_gamepad(static_cast<std::int32_t>(event.gdevice.which), event.gdevice.timestamp);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            close_gamepad(static_cast<std::int32_t>(event.gdevice.which), event.gdevice.timestamp);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            input_snapshot.record_gamepad_axis_event(GamepadAxisEvent{
                .timestamp_ns = event.gaxis.timestamp,
                .instance_id = static_cast<std::int32_t>(event.gaxis.which),
                .axis = event.gaxis.axis,
                .value = event.gaxis.value,
            });
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            input_snapshot.record_gamepad_button_event(GamepadButtonEvent{
                .timestamp_ns = event.gbutton.timestamp,
                .instance_id = static_cast<std::int32_t>(event.gbutton.which),
                .button = event.gbutton.button,
                .down = event.gbutton.down,
            });
            break;
        default:
            break;
        }
    }

    void open_gamepad(std::int32_t instance_id, std::uint64_t timestamp_ns) {
        if (opened_gamepads.contains(instance_id)) {
            return;
        }

        SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
        if (gamepad == nullptr) {
            log->write(foundation::LogLevel::Warning, make_sdl_error_message("SDL_OpenGamepad failed: "));
            return;
        }

        const char* name = SDL_GetGamepadName(gamepad);
        GamepadConnectionEvent connection_event{};
        connection_event.timestamp_ns = timestamp_ns;
        connection_event.device.instance_id = instance_id;
        connection_event.device.name = name != nullptr ? name : "unknown-gamepad";
        connection_event.connected = true;

        opened_gamepads.emplace(instance_id, gamepad);
        input_snapshot.record_gamepad_connection_event(std::move(connection_event));
    }

    void close_gamepad(std::int32_t instance_id, std::uint64_t timestamp_ns) {
        auto gamepad = opened_gamepads.find(instance_id);
        if (gamepad != opened_gamepads.end()) {
            SDL_CloseGamepad(gamepad->second);
            opened_gamepads.erase(gamepad);
        }

        GamepadConnectionEvent connection_event{};
        connection_event.timestamp_ns = timestamp_ns;
        connection_event.device.instance_id = instance_id;
        connection_event.connected = false;
        input_snapshot.record_gamepad_connection_event(std::move(connection_event));
    }

    ApplicationConfig config;
    foundation::CrashSafeLog* log;
    std::thread::id main_thread_id;
    FrameClock frame_clock;
    InputSnapshot input_snapshot;
    WindowState window_state;
    SDL_Window* window{};
    std::unordered_map<std::int32_t, SDL_Gamepad*> opened_gamepads;
    WindowMode windowed_mode_before_fullscreen{WindowMode::Windowed};
    bool initialized{false};
    bool quit_requested{false};
};

SdlApplicationShell::SdlApplicationShell(ApplicationConfig config, foundation::CrashSafeLog& log)
    : impl_(std::make_unique<Impl>(std::move(config), log)) {}

SdlApplicationShell::~SdlApplicationShell() = default;

bool SdlApplicationShell::initialize() {
    return impl_->initialize();
}

void SdlApplicationShell::begin_frame() {
    impl_->begin_frame();
}

void SdlApplicationShell::pump_events() {
    impl_->pump_events();
}

void SdlApplicationShell::present() noexcept {
    impl_->present();
}

void SdlApplicationShell::sleep_until_next_fixed_step() noexcept {
    impl_->sleep_until_next_fixed_step();
}

void SdlApplicationShell::request_quit() noexcept {
    impl_->request_quit();
}

bool SdlApplicationShell::should_quit() const noexcept {
    return impl_->quit_requested;
}

bool SdlApplicationShell::toggle_fullscreen() {
    const WindowMode target_mode = impl_->window_state.mode == WindowMode::Fullscreen
        ? impl_->windowed_mode_before_fullscreen
        : WindowMode::Fullscreen;
    return impl_->apply_window_mode(target_mode);
}

const ApplicationConfig& SdlApplicationShell::config() const noexcept {
    return impl_->config;
}

FrameClock& SdlApplicationShell::frame_clock() noexcept {
    return impl_->frame_clock;
}

const FrameTiming& SdlApplicationShell::frame_timing() const noexcept {
    return impl_->frame_clock.timing();
}

const InputSnapshot& SdlApplicationShell::input_snapshot() const noexcept {
    return impl_->input_snapshot;
}

const WindowState& SdlApplicationShell::window_state() const noexcept {
    return impl_->window_state;
}

} // namespace reaktio::platform