#pragma once

#include "reaktio/gameplay/TypingPrompt.hpp"
#include "reaktio/rhythm/TempoMap.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reaktio::gameplay {

struct TypingErrorPair {
    TypingGrapheme expected;
    TypingGrapheme observed;

    [[nodiscard]] bool operator==(const TypingErrorPair& other) const noexcept {
        return expected == other.expected && observed == other.observed;
    }
};

struct TypingErrorPairHash {
    [[nodiscard]] std::size_t operator()(const TypingErrorPair& pair) const noexcept {
        const std::hash<std::string> hasher{};
        const std::size_t a = hasher(pair.expected);
        const std::size_t b = hasher(pair.observed);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2));
    }
};

struct TypingErrorPatternEntry {
    TypingErrorPair pair;
    std::uint32_t count{};
};

struct TypingErrorPatternSummary {
    std::uint32_t total_mismatches{};
    std::uint32_t total_skips{};
    std::vector<TypingErrorPatternEntry> top_pairs;
};

class TypingErrorPatternTracker {
  public:
    void reset() noexcept;
    void record(const TypingJudgementResult& result);

    [[nodiscard]] std::uint32_t total_mismatches() const noexcept;
    [[nodiscard]] std::uint32_t total_skips() const noexcept;
    [[nodiscard]] std::size_t unique_pair_count() const noexcept;

    [[nodiscard]] TypingErrorPatternSummary summarize(std::size_t top_n = 8) const;

  private:
    std::unordered_map<TypingErrorPair, std::uint32_t, TypingErrorPairHash> pair_counts_;
    std::uint32_t total_mismatches_{};
    std::uint32_t total_skips_{};
};

struct TypingTimingSample {
    std::uint64_t simulation_step{};
    std::uint64_t frame_index{};
    rhythm::TimelineMicroseconds latency_microseconds{};
    TypingJudgement judgement{TypingJudgement::None};
    bool advanced_cursor{};
};

struct TypingTimingHistogram {
    rhythm::TimelineMicroseconds bucket_microseconds{};
    rhythm::TimelineMicroseconds first_bucket_min_microseconds{};
    rhythm::TimelineMicroseconds last_bucket_max_microseconds{};
    std::vector<std::uint32_t> bucket_counts;
    std::uint32_t below_range_count{};
    std::uint32_t above_range_count{};
    std::uint32_t total_samples{};
};

struct TypingTimingSummary {
    std::uint32_t sample_count{};
    rhythm::TimelineMicroseconds min_latency_microseconds{};
    rhythm::TimelineMicroseconds max_latency_microseconds{};
    rhythm::TimelineMicroseconds mean_latency_microseconds{};
    rhythm::TimelineMicroseconds median_latency_microseconds{};
};

struct TypingTimingTrackerOptions {
    static constexpr std::size_t k_default_capacity = 1024;
    static constexpr std::size_t k_default_histogram_buckets = 11;
    static constexpr rhythm::TimelineMicroseconds k_default_bucket_width_microseconds = 50000;

    std::size_t capacity{k_default_capacity};
    std::size_t histogram_bucket_count{k_default_histogram_buckets};
    rhythm::TimelineMicroseconds histogram_bucket_microseconds{k_default_bucket_width_microseconds};
};

class TypingTimingTracker {
  public:
    explicit TypingTimingTracker(TypingTimingTrackerOptions options = {});

    void reset() noexcept;
    void record(const TypingTimingSample& sample);

    [[nodiscard]] const std::deque<TypingTimingSample>& samples() const noexcept;
    [[nodiscard]] std::size_t sample_count() const noexcept;
    [[nodiscard]] std::uint64_t total_samples_recorded() const noexcept;
    [[nodiscard]] bool truncated() const noexcept;

    [[nodiscard]] TypingTimingSummary summarize() const;
    [[nodiscard]] TypingTimingHistogram build_histogram() const;

  private:
    TypingTimingTrackerOptions options_;
    std::deque<TypingTimingSample> samples_;
    std::uint64_t total_recorded_{};
};

} // namespace reaktio::gameplay
