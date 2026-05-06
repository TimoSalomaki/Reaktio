#include "reaktio/gameplay/RailPath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace reaktio::gameplay {

namespace {

constexpr double k_min_segment_length = 1e-9;

[[nodiscard]] double dot(const Vector3& a, const Vector3& b) noexcept {
    return static_cast<double>(a.x) * static_cast<double>(b.x) +
           static_cast<double>(a.y) * static_cast<double>(b.y) +
           static_cast<double>(a.z) * static_cast<double>(b.z);
}

[[nodiscard]] Vector3 cross(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] double length(const Vector3& v) noexcept {
    return std::sqrt(dot(v, v));
}

[[nodiscard]] Vector3 sub(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Vector3 add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Vector3 scale(const Vector3& v, double s) noexcept {
    const float fs = static_cast<float>(s);
    return Vector3{v.x * fs, v.y * fs, v.z * fs};
}

[[nodiscard]] Vector3 normalize_or_default(const Vector3& v, const Vector3& fallback) noexcept {
    const double len = length(v);
    if (len < k_min_segment_length) {
        return fallback;
    }
    const double inv = 1.0 / len;
    return Vector3{
        static_cast<float>(v.x * inv),
        static_cast<float>(v.y * inv),
        static_cast<float>(v.z * inv),
    };
}

[[nodiscard]] Vector3 lerp(const Vector3& a, const Vector3& b, double t) noexcept {
    const float ft = static_cast<float>(t);
    return Vector3{
        a.x + (b.x - a.x) * ft,
        a.y + (b.y - a.y) * ft,
        a.z + (b.z - a.z) * ft,
    };
}

} // namespace

void RailPath::clear() noexcept {
    control_points_.clear();
    cumulative_lengths_.clear();
    statistics_ = RailPathStatistics{};
    valid_ = false;
}

bool RailPath::rebuild(std::vector<RailPathControlPoint> control_points) {
    clear();
    if (control_points.size() < 2) {
        control_points_ = std::move(control_points);
        return false;
    }

    control_points_ = std::move(control_points);
    cumulative_lengths_.reserve(control_points_.size());
    cumulative_lengths_.push_back(0.0);

    statistics_.control_point_count = control_points_.size();
    statistics_.segment_count = control_points_.size() - 1;
    statistics_.shortest_segment_length = std::numeric_limits<double>::infinity();
    statistics_.longest_segment_length = 0.0;

    double accumulated = 0.0;
    bool any_zero_segment = false;
    for (std::size_t i = 1; i < control_points_.size(); ++i) {
        const Vector3 delta = sub(control_points_[i].position, control_points_[i - 1].position);
        const double segment_length = length(delta);
        if (segment_length < k_min_segment_length) {
            any_zero_segment = true;
        }
        statistics_.shortest_segment_length =
            std::min(statistics_.shortest_segment_length, segment_length);
        statistics_.longest_segment_length =
            std::max(statistics_.longest_segment_length, segment_length);
        accumulated += segment_length;
        cumulative_lengths_.push_back(accumulated);
    }

    if (any_zero_segment) {
        // Reject paths with degenerate segments. Modes that need a "stop"
        // should encode it through transport stops, not duplicate control
        // points, otherwise sampling becomes ambiguous.
        clear();
        return false;
    }

    statistics_.total_length = accumulated;
    valid_ = accumulated > k_min_segment_length;
    return valid_;
}

bool RailPath::valid() const noexcept { return valid_; }
double RailPath::total_length() const noexcept { return statistics_.total_length; }
std::size_t RailPath::segment_count() const noexcept { return statistics_.segment_count; }
std::size_t RailPath::control_point_count() const noexcept { return statistics_.control_point_count; }
std::span<const RailPathControlPoint> RailPath::control_points() const noexcept {
    return std::span<const RailPathControlPoint>(control_points_);
}
const RailPathStatistics& RailPath::statistics() const noexcept { return statistics_; }

RailPathSample RailPath::sample_segment(
    std::size_t segment_index, double segment_alpha, double arc_length) const noexcept {
    const RailPathControlPoint& a = control_points_[segment_index];
    const RailPathControlPoint& b = control_points_[segment_index + 1];

    RailPathSample sample{};
    sample.position = lerp(a.position, b.position, segment_alpha);
    sample.tangent = normalize_or_default(sub(b.position, a.position), Vector3{0.0f, 0.0f, 1.0f});

    // Build a stable orthonormal frame: pick the up_hint, project out the
    // component along the tangent so up stays perpendicular, then renormalize.
    const Vector3 up_hint_avg = lerp(a.up_hint, b.up_hint, segment_alpha);
    const double along = dot(up_hint_avg, sample.tangent);
    Vector3 up_perp = sub(up_hint_avg, scale(sample.tangent, along));
    sample.normal = normalize_or_default(up_perp, Vector3{0.0f, 1.0f, 0.0f});

    // Right-handed frame: binormal = tangent x normal (points to the right
    // when looking forward along the tangent with normal pointing up).
    sample.binormal = normalize_or_default(
        cross(sample.tangent, sample.normal), Vector3{1.0f, 0.0f, 0.0f});

    sample.arc_length = arc_length;
    sample.segment_index = segment_index;
    sample.segment_alpha = segment_alpha;
    return sample;
}

RailPathSample RailPath::sample_at_arc_length(double arc_length) const noexcept {
    if (!valid_) {
        return RailPathSample{};
    }
    const double clamped = std::clamp(arc_length, 0.0, statistics_.total_length);

    // Binary search the cumulative_lengths_ for the segment containing the
    // clamped arc length. cumulative_lengths_[i] is the arc length at the
    // *start* of segment i. Segment i spans [cumulative_lengths_[i],
    // cumulative_lengths_[i+1]).
    auto it = std::upper_bound(cumulative_lengths_.begin(), cumulative_lengths_.end(), clamped);
    std::size_t segment_index = (it == cumulative_lengths_.begin())
        ? 0
        : static_cast<std::size_t>(std::distance(cumulative_lengths_.begin(), it) - 1);
    if (segment_index >= statistics_.segment_count) {
        segment_index = statistics_.segment_count - 1;
    }

    const double segment_start = cumulative_lengths_[segment_index];
    const double segment_end = cumulative_lengths_[segment_index + 1];
    const double segment_length = segment_end - segment_start;
    const double segment_alpha = segment_length > k_min_segment_length
        ? std::clamp((clamped - segment_start) / segment_length, 0.0, 1.0)
        : 0.0;

    return sample_segment(segment_index, segment_alpha, clamped);
}

RailPathSample RailPath::sample_at_alpha(double alpha) const noexcept {
    const double clamped = std::clamp(alpha, 0.0, 1.0);
    return sample_at_arc_length(clamped * statistics_.total_length);
}

double RailPath::wrap_arc_length(double arc_length) const noexcept {
    if (!valid_ || statistics_.total_length <= 0.0) {
        return 0.0;
    }
    const double total = statistics_.total_length;
    double wrapped = std::fmod(arc_length, total);
    if (wrapped < 0.0) {
        wrapped += total;
    }
    return wrapped;
}

Vector3 rail_lane_position(
    const RailPathSample& sample,
    std::int32_t signed_lane_index,
    const RailLaneLayout& layout) noexcept {
    const Vector3 lateral = scale(sample.binormal,
        static_cast<double>(signed_lane_index) * layout.lane_spacing);
    const Vector3 vertical = scale(sample.normal, layout.vertical_offset);
    return add(add(sample.position, lateral), vertical);
}

void sample_parallax_stack(
    const ParallaxLayerStack& stack,
    double camera_arc_length,
    std::vector<ParallaxLayerSample>& out_samples) {
    out_samples.clear();
    out_samples.reserve(stack.layers.size());
    for (const ParallaxLayer& layer : stack.layers) {
        ParallaxLayerSample sample{};
        sample.offset = layer.base_offset + camera_arc_length * layer.speed_scalar;
        sample.vertical_offset = layer.vertical_offset;
        sample.tint = layer.tint;
        out_samples.push_back(sample);
    }
}

RailCameraSample sample_rail_camera(const RailPath& path, const RailCameraRig& rig) noexcept {
    RailCameraSample camera{};
    camera.field_of_view_radians = rig.field_of_view_radians;
    camera.near_plane = rig.near_plane;
    camera.far_plane = rig.far_plane;
    if (!path.valid()) {
        return camera;
    }

    const double effective_look_at = rig.wrap_arc_length
        ? path.wrap_arc_length(rig.look_at_arc_length)
        : rig.look_at_arc_length;
    const double effective_eye = rig.wrap_arc_length
        ? path.wrap_arc_length(rig.look_at_arc_length - rig.follow_distance)
        : (rig.look_at_arc_length - rig.follow_distance);

    const RailPathSample target_sample = path.sample_at_arc_length(effective_look_at);
    const RailPathSample eye_sample = path.sample_at_arc_length(effective_eye);

    const Vector3 lateral_at_target = scale(target_sample.binormal, rig.lateral_offset);
    const Vector3 vertical_at_target = scale(target_sample.normal, rig.vertical_offset);
    camera.target = add(add(target_sample.position, lateral_at_target), vertical_at_target);

    const Vector3 lateral_at_eye = scale(eye_sample.binormal, rig.lateral_offset);
    const Vector3 vertical_at_eye = scale(eye_sample.normal, rig.vertical_offset);
    camera.eye = add(add(eye_sample.position, lateral_at_eye), vertical_at_eye);

    camera.up = eye_sample.normal;
    return camera;
}

} // namespace reaktio::gameplay
