#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace reaktio::foundation {

struct RuntimeBudget {
    double target_frame_ms{16.67};
    double simulation_budget_ms{2.0};
    double render_submission_budget_ms{3.0};
    double audio_callback_budget_ms{1.0};
    std::size_t resident_memory_budget_mib{512};
    std::uint32_t draw_call_budget{1500};
    std::uint32_t visible_cue_budget{10000};
};

struct TelemetrySnapshot {
    double frame_ms{};
    double simulation_ms{};
    double render_submission_ms{};
    double audio_drift_ms{};
    std::size_t resident_memory_mib{};
    std::uint32_t draw_calls{};
    std::uint32_t visible_cues{};
};

class TelemetryRecorder {
  public:
    void record(TelemetrySnapshot snapshot);

    [[nodiscard]] const TelemetrySnapshot* last() const noexcept;
    [[nodiscard]] TelemetrySnapshot* last_mutable() noexcept;
    [[nodiscard]] std::span<const TelemetrySnapshot> history() const noexcept;

  private:
    std::vector<TelemetrySnapshot> history_;
};

[[nodiscard]] RuntimeBudget make_bootstrap_budget() noexcept;
[[nodiscard]] bool within_budget(const TelemetrySnapshot& snapshot, const RuntimeBudget& budget) noexcept;
[[nodiscard]] std::string describe_budget_report(
    const TelemetrySnapshot& snapshot,
    const RuntimeBudget& budget);

} // namespace reaktio::foundation