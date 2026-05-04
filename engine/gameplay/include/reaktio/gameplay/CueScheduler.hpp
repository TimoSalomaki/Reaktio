#pragma once

#include "reaktio/gameplay/Transport.hpp"
#include "reaktio/rhythm/CueTravelModel.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace reaktio::gameplay {

struct CueSchedulerRules {
    rhythm::CueTravelWindow spawn_window{};
    bool reset_on_transport_discontinuity{true};
    bool schedule_when_stopped{};
};

enum class CueSchedulerEventKind : std::uint8_t {
    Spawned,
    Updated,
    Despawned,
};

struct ActiveCue {
    std::uint64_t cue_id{};
    std::size_t schedule_index{};
    rhythm::ScheduledCue cue{};
    rhythm::CueTravelState travel_state{};
};

struct CueSchedulerEvent {
    CueSchedulerEventKind kind{CueSchedulerEventKind::Updated};
    ActiveCue cue{};
    TransportDiscontinuityReason discontinuity_reason{TransportDiscontinuityReason::None};
};

struct CueSchedulerSummary {
    std::size_t scheduled_cue_count{};
    std::size_t active_cue_count{};
    std::size_t spawned_this_update{};
    std::size_t updated_this_update{};
    std::size_t despawned_this_update{};
    std::uint64_t reset_count{};
    std::uint64_t transport_timeline_revision{};
    rhythm::ChartTick current_tick{};
};

struct CueSchedulerUpdateInput {
    const rhythm::TempoMap* tempo_map{};
    const TransportSnapshot* transport{};
    std::span<const rhythm::ScheduledCue> schedule{};
    CueSchedulerRules rules{};
};

class CueScheduler {
  public:
    void reset() noexcept;
    void update(const CueSchedulerUpdateInput& input);

    [[nodiscard]] std::span<const ActiveCue> active_cues() const noexcept;
    [[nodiscard]] std::span<const CueSchedulerEvent> events() const noexcept;
    [[nodiscard]] const CueSchedulerSummary& summary() const noexcept;
    [[nodiscard]] bool is_active(std::size_t schedule_index) const noexcept;

  private:
    [[nodiscard]] ActiveCue* find_active(std::size_t schedule_index) noexcept;
    [[nodiscard]] const ActiveCue* find_active(std::size_t schedule_index) const noexcept;
    [[nodiscard]] std::uint64_t make_cue_id(std::size_t schedule_index) const noexcept;
    void despawn_all(
        const rhythm::TempoMap& tempo_map,
        rhythm::ChartTick current_tick,
        const CueSchedulerRules& rules,
        TransportDiscontinuityReason reason);

    std::vector<ActiveCue> active_cues_;
    std::vector<CueSchedulerEvent> events_;
    CueSchedulerSummary summary_{};
    std::uint64_t next_cue_generation_{1};
    std::uint64_t last_transport_timeline_revision_{};
    bool has_transport_timeline_revision_{};
};

} // namespace reaktio::gameplay