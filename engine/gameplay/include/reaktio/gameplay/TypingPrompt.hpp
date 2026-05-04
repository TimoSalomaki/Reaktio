#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace reaktio::gameplay {

// A grapheme cluster represented as its raw UTF-8 bytes. Modes treat this as
// the unit of comparison so multi-codepoint sequences (combining marks, ZWJ
// pairs, regional indicators) stay together as the user perceives them.
using TypingGrapheme = std::string;

struct TypingPromptMetadata {
    std::string id;
    std::string display_name;
    std::string layout_hint;  // optional: "en-US", "fi-FI", "jp-romaji", etc.
    std::string locale_tag;   // optional: BCP-47 tag, opaque to the engine.
};

struct TypingPrompt {
    TypingPromptMetadata metadata;
    std::vector<TypingGrapheme> graphemes;
};

// Predicate that decides whether the next decoded codepoint should attach to
// the previous grapheme cluster. The default implementation handles a small,
// conservative set of well-known combining categories. A future caller can
// supply a richer Unicode segmentation oracle without touching this header.
using GraphemeBreakOracle = std::function<bool(std::uint32_t previous_codepoint, std::uint32_t next_codepoint)>;

[[nodiscard]] GraphemeBreakOracle default_grapheme_break_oracle();

// Decodes a single UTF-8 codepoint starting at `byte_offset`. Returns false
// when the offset is past the end of the buffer or the encoded sequence is
// malformed.
[[nodiscard]] bool decode_utf8_codepoint(
    std::string_view text,
    std::size_t byte_offset,
    std::uint32_t& out_codepoint,
    std::size_t& out_byte_length) noexcept;

[[nodiscard]] std::vector<TypingGrapheme> decode_typing_graphemes(
    std::string_view text,
    const GraphemeBreakOracle& oracle = {});

[[nodiscard]] TypingPrompt make_typing_prompt(
    TypingPromptMetadata metadata,
    std::string_view text,
    const GraphemeBreakOracle& oracle = {});

struct TypingLeniency {
    bool case_insensitive{false};
    bool collapse_whitespace_runs{false};
    // When the user types whitespace at cursor 0 while the prompt expects a
    // non-whitespace grapheme, the input is silently consumed rather than
    // counted as a mismatch.
    bool ignore_leading_whitespace{false};
    // When the cursor reaches the trailing-whitespace tail of the prompt
    // (the suffix consisting only of ASCII whitespace graphemes), the
    // remaining trailing graphemes are silently consumed and the prompt is
    // treated as complete. Has no effect for prompts that do not end in
    // whitespace.
    bool ignore_trailing_whitespace{false};
    // Allow the cursor to advance past a missed grapheme (records a miss but
    // keeps the prompt moving forward); when false, the cursor stalls until
    // the user types the expected grapheme.
    bool skip_on_mismatch{false};
    // Mismatches reset combo. Some training modes prefer to keep combo if
    // the user self-corrects within the same grapheme.
    bool preserve_combo_on_mismatch{false};
};

enum class TypingJudgement : std::uint8_t {
    None,
    Match,
    MatchLenient,
    Mismatch,
    Skipped,
    IgnoredWhitespace,
    PromptComplete,
};

[[nodiscard]] std::string_view to_string(TypingJudgement judgement) noexcept;

struct TypingJudgementResult {
    TypingJudgement judgement{TypingJudgement::None};
    std::size_t prompt_index{};                // index of the grapheme that was judged.
    TypingGrapheme expected_grapheme;
    TypingGrapheme observed_grapheme;
    bool advanced_cursor{};
    bool advances_combo{};
    bool resets_combo{};
};

class TypingCursor {
  public:
    void reset(const TypingPrompt& prompt) noexcept;

    [[nodiscard]] const TypingPrompt* prompt() const noexcept;
    [[nodiscard]] std::size_t cursor() const noexcept;
    [[nodiscard]] std::size_t length() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

    [[nodiscard]] std::uint32_t hit_count() const noexcept;
    [[nodiscard]] std::uint32_t miss_count() const noexcept;
    [[nodiscard]] std::uint32_t skip_count() const noexcept;
    [[nodiscard]] std::uint32_t lenient_match_count() const noexcept;
    [[nodiscard]] std::uint32_t combo() const noexcept;
    [[nodiscard]] std::uint32_t max_combo() const noexcept;

    [[nodiscard]] const TypingGrapheme* expected() const noexcept;
    [[nodiscard]] const TypingJudgementResult& last_result() const noexcept;

    TypingJudgementResult accept(std::string_view utf8_text, const TypingLeniency& leniency = {});
    TypingJudgementResult accept_grapheme(const TypingGrapheme& grapheme, const TypingLeniency& leniency = {});

  private:
    [[nodiscard]] bool graphemes_equal(
        const TypingGrapheme& expected,
        const TypingGrapheme& observed,
        const TypingLeniency& leniency,
        bool& out_lenient) const;

    const TypingPrompt* prompt_{};
    std::size_t cursor_{};
    std::size_t effective_finish_index_{};
    std::uint32_t hit_count_{};
    std::uint32_t miss_count_{};
    std::uint32_t skip_count_{};
    std::uint32_t lenient_match_count_{};
    std::uint32_t combo_{};
    std::uint32_t max_combo_{};
    TypingJudgementResult last_result_{};
};

struct TypingGraphemeStats {
    std::uint32_t presented{};
    std::uint32_t hit{};
    std::uint32_t miss{};
    std::uint32_t skipped{};
    std::uint32_t lenient_hit{};
};

struct TypingAnalyticsSummary {
    std::uint32_t total_judgements{};
    std::uint32_t total_hits{};
    std::uint32_t total_misses{};
    std::uint32_t total_skips{};
    std::uint32_t total_lenient_hits{};
    std::uint32_t total_ignored_whitespace{};
    std::uint32_t prompt_complete_count{};
    double accuracy_ratio{};
    std::size_t unique_grapheme_count{};
};

class TypingAnalytics {
  public:
    void reset() noexcept;
    void record(const TypingJudgementResult& result);
    void record_prompt_completion() noexcept;

    [[nodiscard]] const TypingGraphemeStats* find_stats(const TypingGrapheme& grapheme) const noexcept;
    [[nodiscard]] TypingAnalyticsSummary summary() const noexcept;
    [[nodiscard]] const std::unordered_map<TypingGrapheme, TypingGraphemeStats>& per_grapheme() const noexcept;

  private:
    std::unordered_map<TypingGrapheme, TypingGraphemeStats> per_grapheme_;
    TypingAnalyticsSummary summary_{};
};

} // namespace reaktio::gameplay
