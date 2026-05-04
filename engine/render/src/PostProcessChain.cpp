#include "PostProcessChain.hpp"

#include "reaktio/foundation/CrashSafeLog.hpp"

#define BGFX_PLATFORM_SUPPORTS_WGSL 0
#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <bx/math.h>

#include <dxbc/fs_post_bloom_extract.sc.bin.h>
#include <dxbc/fs_post_blur.sc.bin.h>
#include <dxbc/fs_post_composite.sc.bin.h>
#include <dxbc/fs_post_copy.sc.bin.h>
#include <dxbc/vs_post_fullscreen.sc.bin.h>
#include <dxil/fs_post_bloom_extract.sc.bin.h>
#include <dxil/fs_post_blur.sc.bin.h>
#include <dxil/fs_post_composite.sc.bin.h>
#include <dxil/fs_post_copy.sc.bin.h>
#include <dxil/vs_post_fullscreen.sc.bin.h>
#include <essl/fs_post_bloom_extract.sc.bin.h>
#include <essl/fs_post_blur.sc.bin.h>
#include <essl/fs_post_composite.sc.bin.h>
#include <essl/fs_post_copy.sc.bin.h>
#include <essl/vs_post_fullscreen.sc.bin.h>
#include <glsl/fs_post_bloom_extract.sc.bin.h>
#include <glsl/fs_post_blur.sc.bin.h>
#include <glsl/fs_post_composite.sc.bin.h>
#include <glsl/fs_post_copy.sc.bin.h>
#include <glsl/vs_post_fullscreen.sc.bin.h>
#include <spirv/fs_post_bloom_extract.sc.bin.h>
#include <spirv/fs_post_blur.sc.bin.h>
#include <spirv/fs_post_composite.sc.bin.h>
#include <spirv/fs_post_copy.sc.bin.h>
#include <spirv/vs_post_fullscreen.sc.bin.h>

namespace reaktio::render {

namespace {

constexpr std::uint16_t k_fullscreen_vertex_count = 3u;
constexpr std::uint16_t k_post_process_pass_count = 5u;
bool g_fullscreen_layout_initialized = false;

static const bgfx::EmbeddedShader k_embedded_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_post_fullscreen),
    BGFX_EMBEDDED_SHADER(fs_post_bloom_extract),
    BGFX_EMBEDDED_SHADER(fs_post_blur),
    BGFX_EMBEDDED_SHADER(fs_post_composite),
    BGFX_EMBEDDED_SHADER(fs_post_copy),
    BGFX_EMBEDDED_SHADER_END(),
};

struct FullscreenVertex {
    float x;
    float y;
    float z;
    float u;
    float v;

    static bgfx::VertexLayout layout;

    static void initialize_layout() {
        if (g_fullscreen_layout_initialized) {
            return;
        }

        layout.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        g_fullscreen_layout_initialized = true;
    }
};

bgfx::VertexLayout FullscreenVertex::layout;

bool is_valid(const bgfx::ProgramHandle handle) noexcept {
    return bgfx::isValid(handle);
}

bool is_valid(const bgfx::FrameBufferHandle handle) noexcept {
    return bgfx::isValid(handle);
}

bool is_valid(const bgfx::TextureHandle handle) noexcept {
    return bgfx::isValid(handle);
}

bool is_valid(const bgfx::UniformHandle handle) noexcept {
    return bgfx::isValid(handle);
}

bool is_valid(const bgfx::VertexBufferHandle handle) noexcept {
    return bgfx::isValid(handle);
}

std::string make_message(std::string_view prefix, std::string_view suffix) {
    std::string message(prefix);
    message.append(suffix);
    return message;
}

bgfx::FrameBufferHandle create_color_render_target(std::uint16_t width, std::uint16_t height) {
    constexpr std::uint64_t k_texture_flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    return bgfx::createFrameBuffer(width, height, bgfx::TextureFormat::BGRA8, k_texture_flags);
}

bgfx::FrameBufferHandle create_scene_render_target(std::uint16_t width, std::uint16_t height) {
    constexpr std::uint64_t k_color_flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    constexpr std::uint64_t k_depth_flags = BGFX_TEXTURE_RT_WRITE_ONLY;

    const bgfx::TextureHandle color_texture =
        bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::BGRA8, k_color_flags);
    if (!is_valid(color_texture)) {
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::TextureHandle depth_texture =
        bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D24S8, k_depth_flags);
    if (!is_valid(depth_texture)) {
        bgfx::destroy(color_texture);
        return BGFX_INVALID_HANDLE;
    }

    bgfx::Attachment attachments[2]{};
    attachments[0].init(color_texture, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);
    attachments[1].init(depth_texture, bgfx::Access::Write, 0, 1, 0, BGFX_RESOLVE_NONE);

    const bgfx::FrameBufferHandle frame_buffer = bgfx::createFrameBuffer(2, attachments, true);
    if (!is_valid(frame_buffer)) {
        bgfx::destroy(color_texture);
        bgfx::destroy(depth_texture);
    }

    return frame_buffer;
}

bgfx::VertexBufferHandle create_fullscreen_vertex_buffer(bool origin_bottom_left) {
    FullscreenVertex::initialize_layout();

    const float minx = -1.0f;
    const float maxx = 1.0f;
    const float miny = 0.0f;
    const float maxy = 2.0f;
    const float minu = -1.0f;
    const float maxu = 1.0f;
    const float zz = 0.0f;

    float minv = 0.0f;
    float maxv = 2.0f;
    if (origin_bottom_left) {
        std::swap(minv, maxv);
        minv -= 1.0f;
        maxv -= 1.0f;
    }

    const FullscreenVertex vertices[k_fullscreen_vertex_count] = {
        FullscreenVertex{.x = minx, .y = miny, .z = zz, .u = minu, .v = minv},
        FullscreenVertex{.x = maxx, .y = miny, .z = zz, .u = maxu, .v = minv},
        FullscreenVertex{.x = maxx, .y = maxy, .z = zz, .u = maxu, .v = maxv},
    };

    return bgfx::createVertexBuffer(bgfx::copy(vertices, sizeof(vertices)), FullscreenVertex::layout);
}

} // namespace

struct PostProcessChain::Impl {
    explicit Impl(const platform::PostProcessConfig& post_process_config, foundation::CrashSafeLog& sink) noexcept
        : config(post_process_config),
          log(&sink) {}

    ~Impl() {
        shutdown();
    }

    bool initialize() noexcept {
        if (initialized) {
            return true;
        }

        initialized = true;
        if (!config.enabled || bgfx::getRendererType() == bgfx::RendererType::Noop) {
            available = false;
            return true;
        }

        if (!create_uniforms() || !create_programs() || !create_fullscreen_resources() || !create_fallback_history_texture()) {
            shutdown();
            initialized = true;
            return false;
        }

        available = true;
        return true;
    }

    void shutdown() noexcept {
        destroy_targets();

        if (is_valid(fallback_history_texture)) {
            bgfx::destroy(fallback_history_texture);
            fallback_history_texture = BGFX_INVALID_HANDLE;
        }

        if (is_valid(fullscreen_vertex_buffer)) {
            bgfx::destroy(fullscreen_vertex_buffer);
            fullscreen_vertex_buffer = BGFX_INVALID_HANDLE;
        }

        if (is_valid(program_copy)) {
            bgfx::destroy(program_copy);
            program_copy = BGFX_INVALID_HANDLE;
        }
        if (is_valid(program_composite)) {
            bgfx::destroy(program_composite);
            program_composite = BGFX_INVALID_HANDLE;
        }
        if (is_valid(program_blur)) {
            bgfx::destroy(program_blur);
            program_blur = BGFX_INVALID_HANDLE;
        }
        if (is_valid(program_bloom_extract)) {
            bgfx::destroy(program_bloom_extract);
            program_bloom_extract = BGFX_INVALID_HANDLE;
        }

        destroy_uniform(sampler_scene_color);
        destroy_uniform(sampler_bloom_color);
        destroy_uniform(sampler_feedback_color);
        destroy_uniform(uniform_post_bloom);
        destroy_uniform(uniform_post_color);
        destroy_uniform(uniform_post_feedback);
        destroy_uniform(uniform_post_tint);
        destroy_uniform(uniform_post_blur);

        initialized = false;
        available = false;
        active = false;
        history_valid = false;
        read_feedback_a = true;
        backbuffer_width = 0;
        backbuffer_height = 0;
        bloom_width = 0;
        bloom_height = 0;
    }

    void begin_frame(std::uint16_t requested_backbuffer_width, std::uint16_t requested_backbuffer_height) noexcept {
        active = false;
        if (!available) {
            return;
        }

        if (requested_backbuffer_width == 0 || requested_backbuffer_height == 0) {
            return;
        }

        if (requested_backbuffer_width != backbuffer_width || requested_backbuffer_height != backbuffer_height) {
            if (!create_targets(requested_backbuffer_width, requested_backbuffer_height)) {
                log->write(
                    foundation::LogLevel::Warning,
                    "Renderer post-process targets could not be recreated for the current backbuffer size; disabling post-processing.");
                destroy_targets();
                return;
            }
        }

        active = is_valid(scene_color_frame_buffer) && is_valid(feedback_read_frame_buffer()) &&
            is_valid(feedback_write_frame_buffer()) && is_valid(bloom_ping_frame_buffer) &&
            is_valid(bloom_pong_frame_buffer);
    }

    void configure_views() const noexcept {
        if (!active) {
            set_disabled_view(RenderView::PostBloomExtract);
            set_disabled_view(RenderView::PostBloomBlurHorizontal);
            set_disabled_view(RenderView::PostBloomBlurVertical);
            set_disabled_view(RenderView::PostComposite);
            set_disabled_view(RenderView::PostPresent);
            return;
        }

        float projection[16];
        bx::mtxOrtho(projection, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 100.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);

        bgfx::setViewName(to_view_id(RenderView::PostBloomExtract), "PostBloomExtract");
        bgfx::setViewRect(to_view_id(RenderView::PostBloomExtract), 0, 0, bloom_width, bloom_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::PostBloomExtract), bloom_ping_frame_buffer);
        bgfx::setViewTransform(to_view_id(RenderView::PostBloomExtract), nullptr, projection);
        bgfx::setViewClear(to_view_id(RenderView::PostBloomExtract), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);

        bgfx::setViewName(to_view_id(RenderView::PostBloomBlurHorizontal), "PostBloomBlurHorizontal");
        bgfx::setViewRect(to_view_id(RenderView::PostBloomBlurHorizontal), 0, 0, bloom_width, bloom_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::PostBloomBlurHorizontal), bloom_pong_frame_buffer);
        bgfx::setViewTransform(to_view_id(RenderView::PostBloomBlurHorizontal), nullptr, projection);
        bgfx::setViewClear(to_view_id(RenderView::PostBloomBlurHorizontal), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);

        bgfx::setViewName(to_view_id(RenderView::PostBloomBlurVertical), "PostBloomBlurVertical");
        bgfx::setViewRect(to_view_id(RenderView::PostBloomBlurVertical), 0, 0, bloom_width, bloom_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::PostBloomBlurVertical), bloom_ping_frame_buffer);
        bgfx::setViewTransform(to_view_id(RenderView::PostBloomBlurVertical), nullptr, projection);
        bgfx::setViewClear(to_view_id(RenderView::PostBloomBlurVertical), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);

        bgfx::setViewName(to_view_id(RenderView::PostComposite), "PostComposite");
        bgfx::setViewRect(to_view_id(RenderView::PostComposite), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::PostComposite), feedback_write_frame_buffer());
        bgfx::setViewTransform(to_view_id(RenderView::PostComposite), nullptr, projection);
        bgfx::setViewClear(to_view_id(RenderView::PostComposite), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);

        bgfx::setViewName(to_view_id(RenderView::PostPresent), "PostPresent");
        bgfx::setViewRect(to_view_id(RenderView::PostPresent), 0, 0, backbuffer_width, backbuffer_height);
        bgfx::setViewFrameBuffer(to_view_id(RenderView::PostPresent), BGFX_INVALID_HANDLE);
        bgfx::setViewTransform(to_view_id(RenderView::PostPresent), nullptr, projection);
        bgfx::setViewClear(to_view_id(RenderView::PostPresent), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    }

    void submit() noexcept {
        if (!active) {
            return;
        }

        const bgfx::TextureHandle scene_texture = bgfx::getTexture(scene_color_frame_buffer);
        const bgfx::TextureHandle bloom_ping_texture = bgfx::getTexture(bloom_ping_frame_buffer);
        const bgfx::TextureHandle bloom_pong_texture = bgfx::getTexture(bloom_pong_frame_buffer);
        const bgfx::TextureHandle feedback_read_texture = history_valid
            ? bgfx::getTexture(feedback_read_frame_buffer())
            : fallback_history_texture;
        const bgfx::TextureHandle composite_texture = bgfx::getTexture(feedback_write_frame_buffer());
        submit_bloom_extract(scene_texture);
        submit_blur_pass(RenderView::PostBloomBlurHorizontal, bloom_ping_texture, bloom_pong_frame_buffer, 1.0f, 0.0f);
        submit_blur_pass(RenderView::PostBloomBlurVertical, bloom_pong_texture, bloom_ping_frame_buffer, 0.0f, 1.0f);
        submit_composite(scene_texture, bloom_ping_texture, feedback_read_texture);
        submit_present(composite_texture);

        history_valid = true;
        read_feedback_a = !read_feedback_a;
    }

    [[nodiscard]] bool is_active() const noexcept {
        return active;
    }

    [[nodiscard]] std::uint16_t configured_pass_count() const noexcept {
        return active ? k_post_process_pass_count : 0u;
    }

    [[nodiscard]] bgfx::FrameBufferHandle scene_frame_buffer() const noexcept {
        return active ? scene_color_frame_buffer : bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
    }

  private:
    static void set_disabled_view(RenderView view) noexcept {
        bgfx::setViewRect(to_view_id(view), 0, 0, 1, 1);
        bgfx::setViewFrameBuffer(to_view_id(view), BGFX_INVALID_HANDLE);
        bgfx::setViewClear(to_view_id(view), BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    }

    static void destroy_uniform(bgfx::UniformHandle& handle) noexcept {
        if (is_valid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    bool create_uniforms() noexcept {
        sampler_scene_color = bgfx::createUniform("s_sceneColor", bgfx::UniformType::Sampler);
        sampler_bloom_color = bgfx::createUniform("s_bloomColor", bgfx::UniformType::Sampler);
        sampler_feedback_color = bgfx::createUniform("s_feedbackColor", bgfx::UniformType::Sampler);
        uniform_post_bloom = bgfx::createUniform("u_postBloom", bgfx::UniformType::Vec4);
        uniform_post_color = bgfx::createUniform("u_postColor", bgfx::UniformType::Vec4);
        uniform_post_feedback = bgfx::createUniform("u_postFeedback", bgfx::UniformType::Vec4);
        uniform_post_tint = bgfx::createUniform("u_postTint", bgfx::UniformType::Vec4);
        uniform_post_blur = bgfx::createUniform("u_postBlur", bgfx::UniformType::Vec4);

        return is_valid(sampler_scene_color) && is_valid(sampler_bloom_color) &&
            is_valid(sampler_feedback_color) && is_valid(uniform_post_bloom) &&
            is_valid(uniform_post_color) && is_valid(uniform_post_feedback) &&
            is_valid(uniform_post_tint) && is_valid(uniform_post_blur);
    }

    bool create_programs() noexcept {
        if (!create_program(program_bloom_extract, "vs_post_fullscreen", "fs_post_bloom_extract")) {
            return false;
        }
        if (!create_program(program_blur, "vs_post_fullscreen", "fs_post_blur")) {
            return false;
        }
        if (!create_program(program_composite, "vs_post_fullscreen", "fs_post_composite")) {
            return false;
        }
        return create_program(program_copy, "vs_post_fullscreen", "fs_post_copy");
    }

    bool create_fullscreen_resources() noexcept {
        fullscreen_vertex_buffer = create_fullscreen_vertex_buffer(bgfx::getCaps()->originBottomLeft);
        return is_valid(fullscreen_vertex_buffer);
    }

    bool create_program(bgfx::ProgramHandle& target, const char* vertex_name, const char* fragment_name) noexcept {
        const bgfx::RendererType::Enum renderer_type = bgfx::getRendererType();
        bgfx::ShaderHandle vertex_shader = bgfx::createEmbeddedShader(k_embedded_shaders, renderer_type, vertex_name);
        if (!bgfx::isValid(vertex_shader)) {
            log->write(
                foundation::LogLevel::Warning,
                make_message("Renderer post-process vertex shader was unavailable for the active backend: ", vertex_name));
            return false;
        }

        bgfx::ShaderHandle fragment_shader = bgfx::createEmbeddedShader(k_embedded_shaders, renderer_type, fragment_name);
        if (!bgfx::isValid(fragment_shader)) {
            bgfx::destroy(vertex_shader);
            log->write(
                foundation::LogLevel::Warning,
                make_message("Renderer post-process fragment shader was unavailable for the active backend: ", fragment_name));
            return false;
        }

        target = bgfx::createProgram(vertex_shader, fragment_shader, true);
        return is_valid(target);
    }

    bool create_fallback_history_texture() noexcept {
        constexpr std::uint32_t k_black_pixel = 0x00000000u;
        fallback_history_texture = bgfx::createTexture2D(
            1,
            1,
            false,
            1,
            bgfx::TextureFormat::BGRA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
            bgfx::copy(&k_black_pixel, sizeof(k_black_pixel)));
        return is_valid(fallback_history_texture);
    }

    bool create_targets(std::uint16_t requested_backbuffer_width, std::uint16_t requested_backbuffer_height) noexcept {
        destroy_targets();

        backbuffer_width = requested_backbuffer_width;
        backbuffer_height = requested_backbuffer_height;
        bloom_width = static_cast<std::uint16_t>(std::max(1u, requested_backbuffer_width / 2u));
        bloom_height = static_cast<std::uint16_t>(std::max(1u, requested_backbuffer_height / 2u));

        scene_color_frame_buffer = create_scene_render_target(backbuffer_width, backbuffer_height);
        bloom_ping_frame_buffer = create_color_render_target(bloom_width, bloom_height);
        bloom_pong_frame_buffer = create_color_render_target(bloom_width, bloom_height);
        feedback_a_frame_buffer = create_color_render_target(backbuffer_width, backbuffer_height);
        feedback_b_frame_buffer = create_color_render_target(backbuffer_width, backbuffer_height);
        history_valid = false;
        read_feedback_a = true;

        const bool created = is_valid(scene_color_frame_buffer) && is_valid(bloom_ping_frame_buffer) &&
            is_valid(bloom_pong_frame_buffer) && is_valid(feedback_a_frame_buffer) &&
            is_valid(feedback_b_frame_buffer);
        if (!created) {
            destroy_targets();
        }

        return created;
    }

    void destroy_targets() noexcept {
        destroy_frame_buffer(scene_color_frame_buffer);
        destroy_frame_buffer(bloom_ping_frame_buffer);
        destroy_frame_buffer(bloom_pong_frame_buffer);
        destroy_frame_buffer(feedback_a_frame_buffer);
        destroy_frame_buffer(feedback_b_frame_buffer);
        history_valid = false;
        read_feedback_a = true;
    }

    static void destroy_frame_buffer(bgfx::FrameBufferHandle& handle) noexcept {
        if (is_valid(handle)) {
            bgfx::destroy(handle);
            handle = BGFX_INVALID_HANDLE;
        }
    }

    [[nodiscard]] bgfx::FrameBufferHandle feedback_read_frame_buffer() const noexcept {
        return read_feedback_a ? feedback_a_frame_buffer : feedback_b_frame_buffer;
    }

    [[nodiscard]] bgfx::FrameBufferHandle feedback_write_frame_buffer() const noexcept {
        return read_feedback_a ? feedback_b_frame_buffer : feedback_a_frame_buffer;
    }

    void submit_bloom_extract(bgfx::TextureHandle scene_texture) noexcept {
        const std::array<float, 4> bloom_params = {
            config.bloom_threshold,
            config.bloom_intensity,
            config.bloom_blur_scale,
            0.0f,
        };
        bgfx::setTexture(0, sampler_scene_color, scene_texture);
        bgfx::setUniform(uniform_post_bloom, bloom_params.data());
        submit_fullscreen_pass(RenderView::PostBloomExtract, program_bloom_extract);
    }

    void submit_blur_pass(
        RenderView view,
        bgfx::TextureHandle source_texture,
        bgfx::FrameBufferHandle,
        float direction_x,
        float direction_y) noexcept {
        const std::array<float, 4> blur_params = {
            direction_x,
            direction_y,
            config.bloom_blur_scale,
            0.0f,
        };
        bgfx::setTexture(0, sampler_scene_color, source_texture);
        bgfx::setUniform(uniform_post_blur, blur_params.data());
        submit_fullscreen_pass(view, program_blur);
    }

    void submit_composite(
        bgfx::TextureHandle scene_texture,
        bgfx::TextureHandle bloom_texture,
        bgfx::TextureHandle feedback_texture) noexcept {
        const std::array<float, 4> bloom_params = {
            config.bloom_threshold,
            config.bloom_intensity,
            config.bloom_blur_scale,
            0.0f,
        };
        const std::array<float, 4> color_params = {
            config.exposure,
            config.saturation,
            config.contrast,
            config.vignette_intensity,
        };
        const std::array<float, 4> feedback_params = {
            config.feedback_mix,
            config.feedback_decay,
            config.feedback_scale,
            history_valid ? 1.0f : 0.0f,
        };
        const std::array<float, 4> tint_params = {
            config.color_grade_r,
            config.color_grade_g,
            config.color_grade_b,
            1.0f,
        };

        bgfx::setTexture(0, sampler_scene_color, scene_texture);
        bgfx::setTexture(1, sampler_bloom_color, bloom_texture);
        bgfx::setTexture(2, sampler_feedback_color, feedback_texture);
        bgfx::setUniform(uniform_post_bloom, bloom_params.data());
        bgfx::setUniform(uniform_post_color, color_params.data());
        bgfx::setUniform(uniform_post_feedback, feedback_params.data());
        bgfx::setUniform(uniform_post_tint, tint_params.data());
        submit_fullscreen_pass(RenderView::PostComposite, program_composite);
    }

    void submit_present(bgfx::TextureHandle composed_texture) noexcept {
        bgfx::setTexture(0, sampler_scene_color, composed_texture);
        submit_fullscreen_pass(RenderView::PostPresent, program_copy);
    }

    void submit_fullscreen_pass(RenderView view, bgfx::ProgramHandle program) noexcept {
        if (!is_valid(fullscreen_vertex_buffer)) {
            return;
        }

        bgfx::setVertexBuffer(0, fullscreen_vertex_buffer);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(to_view_id(view), program);
    }

    platform::PostProcessConfig config;
    foundation::CrashSafeLog* log{};
    bgfx::ProgramHandle program_bloom_extract{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle program_blur{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle program_composite{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle program_copy{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_scene_color{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_bloom_color{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle sampler_feedback_color{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle uniform_post_bloom{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle uniform_post_color{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle uniform_post_feedback{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle uniform_post_tint{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle uniform_post_blur{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle fallback_history_texture{BGFX_INVALID_HANDLE};
    bgfx::VertexBufferHandle fullscreen_vertex_buffer{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle scene_color_frame_buffer{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle bloom_ping_frame_buffer{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle bloom_pong_frame_buffer{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle feedback_a_frame_buffer{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle feedback_b_frame_buffer{BGFX_INVALID_HANDLE};
    std::uint16_t backbuffer_width{};
    std::uint16_t backbuffer_height{};
    std::uint16_t bloom_width{};
    std::uint16_t bloom_height{};
    bool initialized{false};
    bool available{false};
    bool active{false};
    bool history_valid{false};
    bool read_feedback_a{true};
};

PostProcessChain::PostProcessChain(const platform::PostProcessConfig& config, foundation::CrashSafeLog& log) noexcept
    : impl_(new Impl(config, log)) {}

PostProcessChain::~PostProcessChain() {
    delete impl_;
    impl_ = nullptr;
}

bool PostProcessChain::initialize() noexcept {
    return impl_->initialize();
}

void PostProcessChain::shutdown() noexcept {
    impl_->shutdown();
}

void PostProcessChain::begin_frame(std::uint16_t backbuffer_width, std::uint16_t backbuffer_height) noexcept {
    impl_->begin_frame(backbuffer_width, backbuffer_height);
}

void PostProcessChain::configure_views() const noexcept {
    impl_->configure_views();
}

void PostProcessChain::submit() noexcept {
    impl_->submit();
}

bool PostProcessChain::active() const noexcept {
    return impl_->is_active();
}

std::uint16_t PostProcessChain::pass_count() const noexcept {
    return impl_->configured_pass_count();
}

bgfx::FrameBufferHandle PostProcessChain::scene_frame_buffer() const noexcept {
    return impl_->scene_frame_buffer();
}

} // namespace reaktio::render