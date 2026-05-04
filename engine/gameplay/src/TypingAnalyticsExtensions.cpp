#include "reaktio/gameplay/TypingAnalyticsExtensions.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace reaktio::gameplay {

namespace {

constexpr std::int64_t microseconds_to_int64(rhythm::TimelineMicroseconds value) noexcept {
    return static_cast<std::int64_t>(value);
}

} // namespace

void TypingErrorPatternTracker::reset() noexcept {
    pair_counts_.clear();
    total_mismatches_ = 0;
    total_skips_ = 0;
}

void TypingErrorPatternTracker::record(const TypingJudgementResult& result) {
    const bool is_mismatch = result.judgement == TypingJudgement::Mismatch;
    const bool is_skip = result.judgement == TypingJudgement::Skipped;
    if (!is_mismatch && !is_skip) {
        return;
    }
    if (result.expected_grapheme.empty()) {
        return;
    }
    if (is_mismatch) {
        ++total_mismatches_;
    } else {
        ++total_skips_;
    }

    TypingErrorPair pair{
        .expected = result.expected_grapheme,
        .observed = result.observed_grapheme,
    };
    auto [it, inserted] = pair_counts_.try_emplace(std::move(pair), 0u);
    ++it->second;
}

std::uint32_t TypingErrorPatternTracker::total_mismatches() const noexcept {
    return total_mismatches_;
}

std::uint32_t TypingErrorPatternTracker::total_skips() const noexcept {
    return total_skips_;
}

std::size_t TypingErrorPatternTracker::unique_pair_count() const noexcept {
    return pair_counts_.size();
}

TypingErrorPatternSummary TypingErrorPatternTracker::summarize(std::size_t top_n) const {
    TypingErrorPatternSummary summary{};
    summary.total_mismatches = total_mismatches_;
    summary.total_skips = total_skips_;
    summary.top_pairs.reserve(pair_counts_.size());
    for (const auto& [pair, count] : pair_counts_) {
        summary.top_pairs.push_back(TypingErrorPatternEntry{.pair = pair, .count = count});
    }
    std::sort(
        summary.top_pairs.begin(),
        summary.top_pairs.end(),
        [](const TypingErrorPatternEntry& lhs, const TypingErrorPatternEntry& rhs) noexcept {
            if (lhs.count != rhs.count) {
                return lhs.count > rhs.count;
            }
            if (lhs.pair.expected != rhs.pair.expected) {
                return lhs.pair.expected < rhs.pair.expected;
            }
            return lhs.pair.observed < rhs.pair.observed;
        });
    if (summary.top_pairs.size() > top_n) {
        summary.top_pairs.resize(top_n);
    }
    return summary;
}

TypingTimingTracker::TypingTimingTracker(TypingTimingTrackerOptions options)
    : options_(options) {
    if (options_.capacity == 0) {
        options_.capacity = TypingTimingTrackerOptions::k_default_capacity;
    }
    if (options_.histogram_bucket_count == 0) {
        options_.histogram_bucket_count = TypingTimingTrackerOptions::k_default_histogram_buckets;
    }
    if (options_.histogram_bucket_microseconds <= 0) {
        options_.histogram_bucket_microseconds =
            TypingTimingTrackerOptions::k_default_bucket_width_microseconds;
    }
}

void TypingTimingTracker::reset() noexcept {
    samples_.clear();
    total_recorded_ = 0;
}

void TypingTimingTracker::record(const TypingTimingSample& sample) {
    samples_.push_back(sample);
    ++total_recorded_;
    if (samples_.size() > options_.capacity) {
        samples_.pop_front();
    }
}

const std::deque<TypingTimingSample>& TypingTimingTracker::samples() const noexcept {
    return samples_;
}

std::size_t TypingTimingTracker::sample_count() const noexcept {
    return samples_.size();
}

std::uint64_t TypingTimingTracker::total_samples_recorded() const noexcept {
    return total_recorded_;
}

bool TypingTimingTracker::truncated() const noexcept {
    return total_recorded_ > samples_.size();
}

TypingTimingSummary TypingTimingTracker::summarize() const {
    TypingTimingSummary summary{};
    summary.sample_count = static_cast<std::uint32_t>(samples_.size());
    if (samples_.empty()) {
        return summary;
    }

    std::int64_t min_value = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_value = std::numeric_limits<std::int64_t>::min();
    long double sum = 0.0L;
    std::vector<std::int64_t> sorted;
    sorted.reserve(samples_.size());
    for (const TypingTimingSample& sample : samples_) {
        const std::int64_t value = microseconds_to_int64(sample.latency_microseconds);
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        sum += static_cast<long double>(value);
        sorted.push_back(value);
    }
    std::sort(sorted.begin(), sorted.end());
    const std::int64_t mean = static_cast<std::int64_t>(sum / static_cast<long double>(samples_.size()));
    const std::int64_t median = sorted[sorted.size() / 2];

    summary.min_latency_microseconds = static_cast<rhythm::TimelineMicroseconds>(min_value);
    summary.max_latency_microseconds = static_cast<rhythm::TimelineMicroseconds>(max_value);
    summary.mean_latency_microseconds = static_cast<rhythm::TimelineMicroseconds>(mean);
    summary.median_latency_microseconds = static_cast<rhythm::TimelineMicroseconds>(median);
    return summary;
}

TypingTimingHistogram TypingTimingTracker::build_histogram() const {
    TypingTimingHistogram histogram{};
    histogram.bucket_microseconds = options_.histogram_bucket_microseconds;
    histogram.bucket_counts.assign(options_.histogram_bucket_count, 0u);
    histogram.first_bucket_min_microseconds = 0;
    histogram.last_bucket_max_microseconds =
        static_cast<rhythm::TimelineMicroseconds>(options_.histogram_bucket_count) *
        histogram.bucket_microseconds;
    histogram.total_samples = static_cast<std::uint32_t>(samples_.size());

    for (const TypingTimingSample& sample : samples_) {
        const std::int64_t value = microseconds_to_int64(sample.latency_microseconds);
        if (value < histogram.first_bucket_min_microseconds) {
            ++histogram.below_range_count;
            continue;
        }
        if (value >= histogram.last_bucket_max_microseconds) {
            ++histogram.above_range_count;
            continue;
        }
        const std::int64_t offset = value - histogram.first_bucket_min_microseconds;
        std::size_t bucket = static_cast<std::size_t>(offset / histogram.bucket_microseconds);
        if (bucket >= histogram.bucket_counts.size()) {
            bucket = histogram.bucket_counts.size() - 1u;
        }
        ++histogram.bucket_counts[bucket];
    }

    return histogram;
}

} // namespace reaktio::gameplay
