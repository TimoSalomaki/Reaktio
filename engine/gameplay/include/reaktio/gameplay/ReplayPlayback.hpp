#pragma once

#include "reaktio/gameplay/ReplayRecorder.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace reaktio::gameplay {

class ReplayPlayer {
  public:
    explicit ReplayPlayer(const ReplaySession& session) noexcept;

    void reset() noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] std::size_t cursor() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] std::size_t input_frame_count() const noexcept;
    [[nodiscard]] std::size_t checkpoint_count() const noexcept;

    [[nodiscard]] const ReplaySession& session() const noexcept;
    [[nodiscard]] const ReplayInputFrame* peek() const noexcept;
    const ReplayInputFrame* advance() noexcept;

    [[nodiscard]] const ReplayInputFrame* find_input_frame(std::uint64_t frame_index) const noexcept;
    [[nodiscard]] const ReplayCheckpoint* find_checkpoint_for_step(std::uint64_t simulation_step) const noexcept;
    [[nodiscard]] const ReplayCheckpoint* find_checkpoint_with_label(std::string_view label) const noexcept;

  private:
    const ReplaySession* session_{};
    std::size_t cursor_{};
};

struct ReplayValidationReport {
    bool ok{};
    std::size_t input_frame_count{};
    std::size_t checkpoint_count{};
    std::uint64_t total_input_frames_recorded{};
    std::uint64_t total_checkpoints_recorded{};
    bool input_frames_truncated{};
    bool checkpoints_truncated{};
    std::uint64_t first_frame_index{};
    std::uint64_t last_frame_index{};
    std::uint64_t first_simulation_step{};
    std::uint64_t last_simulation_step{};
    std::size_t monotonic_frame_violations{};
    std::size_t monotonic_step_violations{};
    std::size_t duplicate_step_count{};
    std::vector<std::string> issues;
};

struct ReplayDivergence {
    std::uint64_t simulation_step{};
    std::uint64_t expected_hash{};
    std::uint64_t observed_hash{};
    std::string label;
};

struct ReplayDivergenceReport {
    std::size_t observed_count{};
    std::size_t matched_count{};
    std::size_t mismatched_count{};
    std::size_t missing_observation_count{};
    std::size_t unexpected_observation_count{};
    std::vector<ReplayDivergence> mismatches;
};

struct ReplayObservedHash {
    std::uint64_t simulation_step{};
    std::uint64_t state_hash{};
};

class ReplayValidator {
  public:
    static constexpr std::size_t k_max_recorded_mismatches = 16;

    [[nodiscard]] ReplayValidationReport validate(const ReplaySession& session) const;
    [[nodiscard]] ReplayDivergenceReport compare_observations(
        const ReplaySession& session,
        std::span<const ReplayObservedHash> observed_hashes) const;
};

} // namespace reaktio::gameplay
