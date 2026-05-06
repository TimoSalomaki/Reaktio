#include "reaktio/gameplay/RailObstacles.hpp"

#include <algorithm>
#include <cstdint>

namespace reaktio::gameplay {

namespace {

[[nodiscard]] bool obstacle_overlaps_arc(
    const RailObstacle& obstacle,
    double arc_length_min,
    double arc_length_max) noexcept {
    const double obstacle_min = obstacle.arc_length - obstacle.arc_length_half_extent;
    const double obstacle_max = obstacle.arc_length + obstacle.arc_length_half_extent;
    return obstacle_max >= arc_length_min && obstacle_min <= arc_length_max;
}

[[nodiscard]] bool obstacle_covers_lane(
    const RailObstacle& obstacle, std::int32_t signed_lane) noexcept {
    return signed_lane >= obstacle.signed_lane_min && signed_lane <= obstacle.signed_lane_max;
}

} // namespace

void RailObstacleField::clear() noexcept {
    obstacles_.clear();
    runtime_hit_points_.clear();
    destroyed_.clear();
    arc_length_sorted_indices_.clear();
}

void RailObstacleField::rebuild(std::vector<RailObstacle> obstacles) {
    clear();
    obstacles_ = std::move(obstacles);
    destroyed_.assign(obstacles_.size(), static_cast<std::uint8_t>(0));
    runtime_hit_points_.resize(obstacles_.size());
    for (std::size_t i = 0; i < obstacles_.size(); ++i) {
        runtime_hit_points_[i] = obstacles_[i].hit_points;
    }

    arc_length_sorted_indices_.resize(obstacles_.size());
    for (std::size_t i = 0; i < obstacles_.size(); ++i) {
        arc_length_sorted_indices_[i] = i;
    }
    // Stable sort by leading edge (arc_length - half_extent) so range queries
    // can short-circuit deterministically once the sorted index sweeps past
    // the upper bound.
    std::stable_sort(
        arc_length_sorted_indices_.begin(),
        arc_length_sorted_indices_.end(),
        [this](std::size_t lhs, std::size_t rhs) {
            const double lhs_lead = obstacles_[lhs].arc_length - obstacles_[lhs].arc_length_half_extent;
            const double rhs_lead = obstacles_[rhs].arc_length - obstacles_[rhs].arc_length_half_extent;
            if (lhs_lead != rhs_lead) {
                return lhs_lead < rhs_lead;
            }
            return obstacles_[lhs].obstacle_id < obstacles_[rhs].obstacle_id;
        });
}

std::span<const RailObstacle> RailObstacleField::obstacles() const noexcept {
    return std::span<const RailObstacle>(obstacles_);
}

std::size_t RailObstacleField::size() const noexcept { return obstacles_.size(); }

void RailObstacleField::reset_runtime_state() noexcept {
    std::fill(destroyed_.begin(), destroyed_.end(), static_cast<std::uint8_t>(0));
    for (std::size_t i = 0; i < obstacles_.size(); ++i) {
        runtime_hit_points_[i] = obstacles_[i].hit_points;
    }
}

void RailObstacleField::query_overlap_point(
    double player_arc_length,
    std::int32_t player_signed_lane,
    std::vector<std::size_t>& out_indices) const {
    query_overlap_range(player_arc_length, player_arc_length, player_signed_lane, out_indices);
}

void RailObstacleField::query_overlap_range(
    double arc_length_min,
    double arc_length_max,
    std::int32_t player_signed_lane,
    std::vector<std::size_t>& out_indices) const {
    if (arc_length_min > arc_length_max) {
        std::swap(arc_length_min, arc_length_max);
    }
    // Sweep the arc-length-sorted index list. Once an obstacle's leading edge
    // is past the query upper bound, no further candidates can match because
    // entries are sorted by leading edge ascending.
    for (std::size_t sorted_index : arc_length_sorted_indices_) {
        const RailObstacle& obstacle = obstacles_[sorted_index];
        const double obstacle_min = obstacle.arc_length - obstacle.arc_length_half_extent;
        if (obstacle_min > arc_length_max) {
            break;
        }
        if (!obstacle_overlaps_arc(obstacle, arc_length_min, arc_length_max)) {
            continue;
        }
        if (!obstacle_covers_lane(obstacle, player_signed_lane)) {
            continue;
        }
        out_indices.push_back(sorted_index);
    }
}

bool RailObstacleField::mark_destroyed(std::size_t obstacle_index) noexcept {
    if (obstacle_index >= destroyed_.size()) {
        return false;
    }
    if (destroyed_[obstacle_index] != static_cast<std::uint8_t>(0)) {
        return false;
    }
    destroyed_[obstacle_index] = static_cast<std::uint8_t>(1);
    runtime_hit_points_[obstacle_index] = 0u;
    return true;
}

bool RailObstacleField::register_hit(
    std::size_t obstacle_index, std::uint32_t damage) noexcept {
    if (obstacle_index >= destroyed_.size()) {
        return false;
    }
    if (destroyed_[obstacle_index] != static_cast<std::uint8_t>(0)) {
        return false;
    }
    if (damage == 0u) {
        return false;
    }
    std::uint32_t& hp = runtime_hit_points_[obstacle_index];
    if (damage >= hp) {
        hp = 0u;
        destroyed_[obstacle_index] = static_cast<std::uint8_t>(1);
        return true;
    }
    hp -= damage;
    return false;
}

bool RailObstacleField::is_destroyed(std::size_t obstacle_index) const noexcept {
    if (obstacle_index >= destroyed_.size()) {
        return false;
    }
    return destroyed_[obstacle_index] != static_cast<std::uint8_t>(0);
}

std::uint32_t RailObstacleField::remaining_hit_points(
    std::size_t obstacle_index) const noexcept {
    if (obstacle_index >= runtime_hit_points_.size()) {
        return 0u;
    }
    return runtime_hit_points_[obstacle_index];
}

void resolve_hit_scan(
    const RailObstacleField& field,
    const RailHitScanProbe& probe,
    std::vector<RailHitScanHit>& out_hits) {
    if (probe.max_distance <= 0.0) {
        return;
    }
    const double range_min = probe.origin_arc_length;
    const double range_max = probe.origin_arc_length + probe.max_distance;

    // Collect candidates in arc-length order. Field obstacles() is the
    // *unsorted* registration order; we need them in arc order to produce
    // the natural "nearest first, then next" hit list. We borrow the sorted
    // index list via a range query on the same axis.
    std::vector<std::size_t> candidates;
    field.query_overlap_range(range_min, range_max, probe.origin_signed_lane, candidates);

    // Sort candidates by distance to origin so pierce ordering is stable.
    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [&](std::size_t lhs, std::size_t rhs) {
            const RailObstacle& l = field.obstacles()[lhs];
            const RailObstacle& r = field.obstacles()[rhs];
            const double l_lead = l.arc_length - l.arc_length_half_extent;
            const double r_lead = r.arc_length - r.arc_length_half_extent;
            if (l_lead != r_lead) {
                return l_lead < r_lead;
            }
            return l.obstacle_id < r.obstacle_id;
        });

    std::uint32_t hits_remaining = 1u + probe.pierce_count;
    for (std::size_t obstacle_index : candidates) {
        if (hits_remaining == 0u) {
            break;
        }
        if (probe.ignore_destroyed && field.is_destroyed(obstacle_index)) {
            continue;
        }
        const RailObstacle& obstacle = field.obstacles()[obstacle_index];
        if ((obstacle.flags & probe.required_flag_mask) == 0u) {
            // No relevant hit flag (typical: not Shootable). Skip without
            // consuming a hit, but also do not let the probe pass through a
            // Solid obstacle that lacks Shootable.
            if (has_flag(obstacle.flags, RailObstacleFlag::Solid)) {
                break;
            }
            continue;
        }
        const double obstacle_lead = obstacle.arc_length - obstacle.arc_length_half_extent;
        const double hit_arc_length = std::max(obstacle_lead, probe.origin_arc_length);
        out_hits.push_back(RailHitScanHit{
            .obstacle_index = obstacle_index,
            .obstacle_id = obstacle.obstacle_id,
            .hit_arc_length = hit_arc_length,
            .distance = hit_arc_length - probe.origin_arc_length,
        });
        --hits_remaining;
        if (has_flag(obstacle.flags, RailObstacleFlag::Solid)) {
            // Solid obstacles always stop the probe even if pierce was set.
            break;
        }
    }
}

void advance_projectiles(
    std::vector<RailProjectile>& projectiles,
    const RailObstacleField& field,
    double fixed_delta_seconds,
    std::vector<RailProjectileHit>& out_hits) {
    if (fixed_delta_seconds <= 0.0) {
        return;
    }
    std::vector<std::size_t> swept;
    for (RailProjectile& projectile : projectiles) {
        if (!projectile.active) {
            continue;
        }
        const double previous_arc = projectile.arc_length;
        const double next_arc = projectile.arc_length + projectile.speed * fixed_delta_seconds;
        const double sweep_min = std::min(previous_arc, next_arc);
        const double sweep_max = std::max(previous_arc, next_arc);

        swept.clear();
        field.query_overlap_range(sweep_min, sweep_max, projectile.signed_lane, swept);

        std::stable_sort(
            swept.begin(),
            swept.end(),
            [&](std::size_t lhs, std::size_t rhs) {
                const RailObstacle& l = field.obstacles()[lhs];
                const RailObstacle& r = field.obstacles()[rhs];
                const double l_lead = l.arc_length - l.arc_length_half_extent;
                const double r_lead = r.arc_length - r.arc_length_half_extent;
                if (l_lead != r_lead) {
                    return l_lead < r_lead;
                }
                return l.obstacle_id < r.obstacle_id;
            });

        bool consumed = false;
        for (std::size_t obstacle_index : swept) {
            if (field.is_destroyed(obstacle_index)) {
                continue;
            }
            const RailObstacle& obstacle = field.obstacles()[obstacle_index];
            if (!has_flag(obstacle.flags, RailObstacleFlag::Shootable) &&
                !has_flag(obstacle.flags, RailObstacleFlag::Solid)) {
                continue;
            }
            const double obstacle_lead = obstacle.arc_length - obstacle.arc_length_half_extent;
            out_hits.push_back(RailProjectileHit{
                .projectile_id = projectile.projectile_id,
                .obstacle_index = obstacle_index,
                .obstacle_id = obstacle.obstacle_id,
                .hit_arc_length = std::max(obstacle_lead, previous_arc),
                .damage = projectile.damage,
            });
            if (projectile.pierce_remaining > 0u &&
                !has_flag(obstacle.flags, RailObstacleFlag::Solid)) {
                --projectile.pierce_remaining;
                continue;
            }
            consumed = true;
            break;
        }

        if (consumed) {
            projectile.active = false;
            continue;
        }
        projectile.arc_length = next_arc;
        projectile.remaining_lifetime -= fixed_delta_seconds;
        if (projectile.remaining_lifetime <= 0.0) {
            projectile.active = false;
        }
    }
}

void RailEnvTriggerStream::clear() noexcept {
    triggers_.clear();
    cursor_ = 0;
    last_arc_length_ = 0.0;
}

void RailEnvTriggerStream::rebuild(std::vector<RailEnvTrigger> triggers) {
    triggers_ = std::move(triggers);
    std::stable_sort(
        triggers_.begin(),
        triggers_.end(),
        [](const RailEnvTrigger& lhs, const RailEnvTrigger& rhs) {
            if (lhs.arc_length != rhs.arc_length) {
                return lhs.arc_length < rhs.arc_length;
            }
            return lhs.trigger_id < rhs.trigger_id;
        });
    cursor_ = 0;
    last_arc_length_ = 0.0;
}

std::span<const RailEnvTrigger> RailEnvTriggerStream::triggers() const noexcept {
    return std::span<const RailEnvTrigger>(triggers_);
}

std::size_t RailEnvTriggerStream::fired_count() const noexcept { return cursor_; }

void RailEnvTriggerStream::reset_runtime_state() noexcept {
    cursor_ = 0;
    last_arc_length_ = 0.0;
}

void RailEnvTriggerStream::advance_to_arc_length(
    double player_arc_length,
    std::vector<RailEnvTrigger>& out_fired) {
    // Modes that loop the rail should call reset_runtime_state() at the loop
    // boundary; the stream does not auto-wrap because trigger ordering is
    // domain-specific (e.g. some modes want re-fires per loop, others not).
    if (player_arc_length < last_arc_length_) {
        last_arc_length_ = player_arc_length;
    }
    while (cursor_ < triggers_.size() && triggers_[cursor_].arc_length <= player_arc_length) {
        out_fired.push_back(triggers_[cursor_]);
        ++cursor_;
    }
    last_arc_length_ = player_arc_length;
}

} // namespace reaktio::gameplay
