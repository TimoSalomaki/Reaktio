cmake_minimum_required(VERSION 3.25)

project(Reaktio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(REAKTIO_WARNINGS_AS_ERRORS "Treat warnings as errors for Reaktio targets." ON)
option(REAKTIO_ENABLE_ASAN "Enable AddressSanitizer for Reaktio targets." OFF)
option(REAKTIO_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for Reaktio targets." OFF)
option(REAKTIO_ENABLE_CLANG_TIDY "Enable clang-tidy during builds for Reaktio targets." OFF)
option(REAKTIO_ENABLE_CPPCHECK "Enable cppcheck during builds for Reaktio targets." OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(ReaktioCompilerOptions)
include(ReaktioSanitizers)
include(ReaktioStaticAnalysis)
include(ReaktioDependencies)
include(ReaktioDevTargets)

reaktio_configure_dependencies()

function(reaktio_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    reaktio_apply_compiler_options(${target_name})
    reaktio_enable_sanitizers(${target_name})
    reaktio_enable_static_analysis(${target_name})
endfunction()

add_library(reaktio_platform_api INTERFACE)
add_library(Reaktio::platform_api ALIAS reaktio_platform_api)
target_include_directories(reaktio_platform_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/include")

add_library(reaktio_entt INTERFACE)
add_library(Reaktio::entt ALIAS reaktio_entt)
target_include_directories(reaktio_entt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/external/entt/src")

add_library(reaktio_render_api INTERFACE)
add_library(Reaktio::render_api ALIAS reaktio_render_api)
target_include_directories(reaktio_render_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/engine/render/include")

add_library(reaktio_foundation STATIC
    engine/foundation/src/BuildInfo.cpp
    engine/foundation/src/CrashSafeLog.cpp
    engine/foundation/src/DeterministicRandom.cpp
    engine/foundation/src/ResourceRegistry.cpp
    engine/foundation/src/Telemetry.cpp
)
add_library(Reaktio::foundation ALIAS reaktio_foundation)
target_include_directories(reaktio_foundation
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/foundation/include"
)
target_compile_definitions(reaktio_foundation
    PUBLIC
        REAKTIO_PROJECT_NAME="${PROJECT_NAME}"
        REAKTIO_PROJECT_VERSION="${PROJECT_VERSION}"
    REAKTIO_BUILD_CONFIGURATION="$<CONFIG>"
)
reaktio_configure_target(reaktio_foundation)

add_library(reaktio_rhythm STATIC
    engine/rhythm/src/CueTravelModel.cpp
    engine/rhythm/src/TempoMap.cpp
    engine/rhythm/src/TimingJudgement.cpp
)
add_library(Reaktio::rhythm ALIAS reaktio_rhythm)
target_include_directories(reaktio_rhythm
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/rhythm/include"
)
target_link_libraries(reaktio_rhythm PUBLIC Reaktio::foundation)
reaktio_configure_target(reaktio_rhythm)

add_library(reaktio_audio STATIC
    engine/audio/src/AudioClipLibrary.cpp
)
add_library(Reaktio::audio ALIAS reaktio_audio)
target_include_directories(reaktio_audio
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/audio/include"
)
target_link_libraries(reaktio_audio PUBLIC Reaktio::foundation SDL3::SDL3)
reaktio_configure_target(reaktio_audio)

add_library(reaktio_gameplay STATIC
    engine/gameplay/src/DebugVisualizations.cpp
    engine/gameplay/src/EventBus.cpp
    engine/gameplay/src/GameModeRegistry.cpp
    engine/gameplay/src/ModeConfiguration.cpp
    engine/gameplay/src/MotionCollision.cpp
    engine/gameplay/src/ReplayRecorder.cpp
    engine/gameplay/src/Transforms.cpp
    engine/gameplay/src/TransportController.cpp
    engine/gameplay/src/WorldModel.cpp
)
add_library(Reaktio::gameplay ALIAS reaktio_gameplay)
target_include_directories(reaktio_gameplay
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/gameplay/include"
)
target_link_libraries(reaktio_gameplay PUBLIC Reaktio::entt Reaktio::foundation Reaktio::platform_api Reaktio::render_api)
reaktio_configure_target(reaktio_gameplay)

add_library(reaktio_platform STATIC
    engine/platform/src/FrameClock.cpp
    engine/platform/src/InputSnapshot.cpp
    engine/platform/src/SdlAudioDevice.cpp
    engine/platform/src/SdlApplicationShell.cpp
    engine/platform/src/StackProbe.cpp
)
add_library(Reaktio::platform ALIAS reaktio_platform)
target_include_directories(reaktio_platform
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/platform/include"
)
target_link_libraries(reaktio_platform PUBLIC Reaktio::foundation Reaktio::platform_api SDL3::SDL3 bgfx)
reaktio_configure_target(reaktio_platform)

add_library(reaktio_render STATIC
    engine/render/src/CookedAssetLibrary.cpp
    engine/render/src/InstanceBufferAllocator.cpp
    engine/render/src/MaterialRegistry.cpp
    engine/render/src/RenderCamera.cpp
    engine/render/src/RenderExtraction.cpp
    engine/render/src/RenderSubsystem.cpp
    engine/render/src/ShaderProgramRegistry.cpp
    engine/render/src/TransientBufferAllocator.cpp
    engine/render/src/UniformRegistry.cpp
)
add_library(Reaktio::render ALIAS reaktio_render)
target_include_directories(reaktio_render
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/render/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/external/bgfx.cmake/bx/include"
)
target_link_libraries(reaktio_render PUBLIC Reaktio::foundation Reaktio::platform Reaktio::render_api bgfx)
reaktio_configure_target(reaktio_render)

add_library(reaktio_starter_mode STATIC
    games/templates/starter_mode/src/StarterMode.cpp
)
add_library(Reaktio::starter_mode ALIAS reaktio_starter_mode)
target_include_directories(reaktio_starter_mode
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/games/templates/starter_mode/include"
)
target_link_libraries(reaktio_starter_mode PUBLIC Reaktio::foundation Reaktio::gameplay)
target_link_libraries(reaktio_starter_mode PUBLIC Reaktio::platform_api Reaktio::render_api)
reaktio_configure_target(reaktio_starter_mode)

add_library(reaktio_reference_sandbox_mode STATIC
    games/reference/sandbox_mode/src/ReferenceSandboxMode.cpp
)
add_library(Reaktio::reference_sandbox_mode ALIAS reaktio_reference_sandbox_mode)
target_include_directories(reaktio_reference_sandbox_mode
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/games/reference/sandbox_mode/include"
)
target_link_libraries(
    reaktio_reference_sandbox_mode
    PUBLIC
        Reaktio::foundation
        Reaktio::gameplay
        Reaktio::platform_api
        Reaktio::rhythm
        Reaktio::render_api
)
reaktio_configure_target(reaktio_reference_sandbox_mode)

add_library(reaktio_app STATIC
    engine/app/src/AuthoritativeAudioTransport.cpp
    engine/app/src/RuntimeConfiguration.cpp
    engine/app/src/SmokeApplication.cpp
    engine/app/src/TransportAlignmentTracker.cpp
    engine/app/src/TransportDrivenAudioSource.cpp
)
add_library(Reaktio::app ALIAS reaktio_app)
target_include_directories(reaktio_app
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/app/include"
)
target_link_libraries(reaktio_app
    PUBLIC
        Reaktio::audio
        Reaktio::foundation
        Reaktio::gameplay
        Reaktio::platform
    Reaktio::render
)
reaktio_configure_target(reaktio_app)

add_executable(reaktio_smoke engine/app/src/main.cpp)
target_link_libraries(
    reaktio_smoke
    PRIVATE
        Reaktio::app
        Reaktio::starter_mode
        Reaktio::reference_sandbox_mode
)
reaktio_configure_target(reaktio_smoke)

if(TARGET shaderc AND TARGET texturec AND TARGET geometryc)
    add_custom_target(reaktio-bootstrap-tools DEPENDS shaderc texturec geometryc)
endif()

if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(
        TARGET reaktio_smoke
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared>
                $<TARGET_FILE_DIR:reaktio_smoke>
        VERBATIM
    )
endif()

reaktio_add_dev_targets()