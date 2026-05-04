#include "reaktio/gameplay/CueScheduler.hpp"

#include <algorithm>

namespace reaktio::gameplay {

namespace {

bool should_schedule(const TransportSnapshot& transport, const CueSchedulerRules& rules) noexcept {
    return rules.schedule_when_stopped || transport.playback_state != TransportPlaybackState::Stopped;
}

} // namespace

void CueScheduler::reset() noexcept {
    active_cues_.clear();
    events_.clear();
    summary_ = {};
    next_cue_generation_ = 1;
    last_transport_timeline_revision_ = 0;
    has_transport_timeline_revision_ = false;
}

void CueScheduler::update(const CueSchedulerUpdateInput& input) {
    events_.clear();
    summary_.scheduled_cue_count = input.schedule.size();
    summary_.spawned_this_update = 0;
    summary_.updated_this_update = 0;
    summary_.despawned_this_update = 0;

    if (input.tempo_map == nullptr || input.transport == nullptr || !input.tempo_map->valid()) {
        active_cues_.clear();
        summary_.active_cue_count = 0;
        return;
    }

    const rhythm::TempoMap& tempo_map = *input.tempo_map;
    const TransportSnapshot& transport = *input.transport;
    const rhythm::RhythmPosition current_position = tempo_map.position_from_seconds(transport.position_seconds);
    summary_.current_tick = current_position.tick;
    summary_.transport_timeline_revision = transport.discontinuity.timeline_revision;

    if (!should_schedule(transport, input.rules)) {
        despawn_all(tempo_map, current_position.tick, input.rules, transport.discontinuity.last_reason);
        summary_.active_cue_count = active_cues_.size();
        return;
    }

    const bool discontinuity = has_transport_timeline_revision_ &&
        transport.discontinuity.timeline_revision != last_transport_timeline_revision_;
    if (discontinuity && input.rules.reset_on_transport_discontinuity) {
        despawn_all(tempo_map, current_position.tick, input.rules, transport.discontinuity.last_reason);
        ++summary_.reset_count;
    }
    last_transport_timeline_revision_ = transport.discontinuity.timeline_revision;
    has_transport_timeline_revision_ = true;

    for (std::size_t index = 0; index < input.schedule.size(); ++index) {
        const rhythm::ScheduledCue& cue = input.schedule[index];
        const rhythm::CueTravelState travel_state = rhythm::sample_cue_travel(
            tempo_map,
            current_position.tick,
            cue,
            input.rules.spawn_window);
        ActiveCue* active = find_active(index);

        if (!travel_state.visible) {
            if (active != nullptr) {
                events_.push_back(CueSchedulerEvent{
                    .kind = CueSchedulerEventKind::Despawned,
                    .cue = *active,
                    .discontinuity_reason = transport.discontinuity.last_reason,
                });
                ++summary_.despawned_this_update;
                active_cues_.erase(std::remove_if(active_cues_.begin(), active_cues_.end(), [index](const ActiveCue& active_cue) {
                    return active_cue.schedule_index == index;
                }), active_cues_.end());
            }
            continue;
        }

        if (active == nullptr) {
            ActiveCue spawned{
                .cue_id = make_cue_id(index),
                .schedule_index = index,
                .cue = cue,
                .travel_state = travel_state,
            };
            active_cues_.push_back(spawned);
            events_.push_back(CueSchedulerEvent{
                .kind = CueSchedulerEventKind::Spawned,
                .cue = spawned,
            });
            ++summary_.spawned_this_update;
            continue;
        }

        active->cue = cue;
        active->travel_state = travel_state;
        events_.push_back(CueSchedulerEvent{
            .kind = CueSchedulerEventKind::Updated,
            .cue = *active,
        });
        ++summary_.updated_this_update;
    }

    std::sort(active_cues_.begin(), active_cues_.end(), [](const ActiveCue& left, const ActiveCue& right) {
        return left.schedule_index < right.schedule_index;
    });
    summary_.active_cue_count = active_cues_.size();
}

std::span<const ActiveCue> CueScheduler::active_cues() const noexcept {
    return std::span<const ActiveCue>{active_cues_.data(), active_cues_.size()};
}

std::span<const CueSchedulerEvent> CueScheduler::events() const noexcept {
    return std::span<const CueSchedulerEvent>{events_.data(), events_.size()};
}

const CueSchedulerSummary& CueScheduler::summary() const noexcept {
    return summary_;
}

bool CueScheduler::is_active(std::size_t schedule_index) const noexcept {
    return find_active(schedule_index) != nullptr;
}

ActiveCue* CueScheduler::find_active(std::size_t schedule_index) noexcept {
    const auto it = std::find_if(active_cues_.begin(), active_cues_.end(), [schedule_index](const ActiveCue& cue) {
        return cue.schedule_index == schedule_index;
    });
    return it != active_cues_.end() ? &(*it) : nullptr;
}

const ActiveCue* CueScheduler::find_active(std::size_t schedule_index) const noexcept {
    const auto it = std::find_if(active_cues_.begin(), active_cues_.end(), [schedule_index](const ActiveCue& cue) {
        return cue.schedule_index == schedule_index;
    });
    return it != active_cues_.end() ? &(*it) : nullptr;
}

std::uint64_t CueScheduler::make_cue_id(std::size_t schedule_index) const noexcept {
    return (next_cue_generation_ << 32u) ^ static_cast<std::uint64_t>(schedule_index + 1u);
}

void CueScheduler::despawn_all(
    const rhythm::TempoMap& tempo_map,
    rhythm::ChartTick current_tick,
    const CueSchedulerRules& rules,
    TransportDiscontinuityReason reason) {
    for (ActiveCue active : active_cues_) {
        active.travel_state = rhythm::sample_cue_travel(tempo_map, current_tick, active.cue, rules.spawn_window);
        events_.push_back(CueSchedulerEvent{
            .kind = CueSchedulerEventKind::Despawned,
            .cue = active,
            .discontinuity_reason = reason,
        });
        ++summary_.despawned_this_update;
    }
    active_cues_.clear();
    ++next_cue_generation_;
}

} // namespace reaktio::gameplay