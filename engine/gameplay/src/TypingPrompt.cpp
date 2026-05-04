#include "reaktio/gameplay/TypingPrompt.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace reaktio::gameplay {

namespace {

bool is_ascii_whitespace_codepoint(std::uint32_t codepoint) noexcept {
    return codepoint == 0x20 || codepoint == 0x09 || codepoint == 0x0a ||
           codepoint == 0x0b || codepoint == 0x0c || codepoint == 0x0d;
}

bool grapheme_is_ascii_whitespace(const TypingGrapheme& grapheme) noexcept {
    if (grapheme.size() != 1) {
        return false;
    }
    return is_ascii_whitespace_codepoint(static_cast<unsigned char>(grapheme.front()));
}

// Conservative attach predicate: combine common combining-mark ranges with the
// previous codepoint. Anything else starts a new grapheme. Modes can override
// by supplying a richer GraphemeBreakOracle without touching this default.
bool default_attaches_to_previous(std::uint32_t previous, std::uint32_t next) noexcept {
    if (previous == 0) {
        return false;
    }
    // Combining Diacritical Marks (U+0300..U+036F)
    if (next >= 0x0300 && next <= 0x036F) {
        return true;
    }
    // Combining Diacritical Marks Extended/Supplement
    if (next >= 0x1AB0 && next <= 0x1AFF) {
        return true;
    }
    if (next >= 0x1DC0 && next <= 0x1DFF) {
        return true;
    }
    // Variation selectors
    if (next >= 0xFE00 && next <= 0xFE0F) {
        return true;
    }
    // Zero-width joiner
    if (next == 0x200D) {
        return true;
    }
    return false;
}

bool ascii_equals_ignore_case(const TypingGrapheme& lhs, const TypingGrapheme& rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto a = static_cast<unsigned char>(lhs[index]);
        const auto b = static_cast<unsigned char>(rhs[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

} // namespace

GraphemeBreakOracle default_grapheme_break_oracle() {
    return [](std::uint32_t previous, std::uint32_t next) noexcept {
        // Returning true means "break before next" (i.e., next starts a new grapheme).
        return !default_attaches_to_previous(previous, next);
    };
}

bool decode_utf8_codepoint(
    std::string_view text,
    std::size_t byte_offset,
    std::uint32_t& out_codepoint,
    std::size_t& out_byte_length) noexcept {
    if (byte_offset >= text.size()) {
        return false;
    }

    const auto first = static_cast<unsigned char>(text[byte_offset]);
    std::uint32_t codepoint = 0;
    std::size_t expected = 0;

    if ((first & 0x80u) == 0) {
        codepoint = first;
        expected = 1;
    } else if ((first & 0xE0u) == 0xC0u) {
        codepoint = first & 0x1Fu;
        expected = 2;
    } else if ((first & 0xF0u) == 0xE0u) {
        codepoint = first & 0x0Fu;
        expected = 3;
    } else if ((first & 0xF8u) == 0xF0u) {
        codepoint = first & 0x07u;
        expected = 4;
    } else {
        return false;
    }

    if (byte_offset + expected > text.size()) {
        return false;
    }

    for (std::size_t continuation = 1; continuation < expected; ++continuation) {
        const auto byte = static_cast<unsigned char>(text[byte_offset + continuation]);
        if ((byte & 0xC0u) != 0x80u) {
            return false;
        }
        codepoint = (codepoint << 6) | (byte & 0x3Fu);
    }

    // Overlong encodings and surrogate codepoints are explicitly rejected.
    if (expected == 2 && codepoint < 0x80) {
        return false;
    }
    if (expected == 3 && codepoint < 0x800) {
        return false;
    }
    if (expected == 4 && codepoint < 0x10000) {
        return false;
    }
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        return false;
    }
    if (codepoint > 0x10FFFF) {
        return false;
    }

    out_codepoint = codepoint;
    out_byte_length = expected;
    return true;
}

std::vector<TypingGrapheme> decode_typing_graphemes(
    std::string_view text,
    const GraphemeBreakOracle& oracle) {
    GraphemeBreakOracle effective = oracle ? oracle : default_grapheme_break_oracle();

    std::vector<TypingGrapheme> graphemes;
    std::size_t byte_offset = 0;
    std::uint32_t previous_codepoint = 0;
    TypingGrapheme current;

    while (byte_offset < text.size()) {
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (!decode_utf8_codepoint(text, byte_offset, codepoint, length)) {
            // Skip a single byte on malformed input rather than aborting the
            // entire prompt; authoring data should still be cooked, but the
            // engine refuses to crash on bad UTF-8.
            byte_offset += 1;
            continue;
        }

        const bool break_here = current.empty() || effective(previous_codepoint, codepoint);
        if (break_here && !current.empty()) {
            graphemes.push_back(std::move(current));
            current.clear();
        }

        current.append(text.substr(byte_offset, length));
        previous_codepoint = codepoint;
        byte_offset += length;
    }
    if (!current.empty()) {
        graphemes.push_back(std::move(current));
    }
    return graphemes;
}

TypingPrompt make_typing_prompt(
    TypingPromptMetadata metadata,
    std::string_view text,
    const GraphemeBreakOracle& oracle) {
    TypingPrompt prompt{};
    prompt.metadata = std::move(metadata);
    prompt.graphemes = decode_typing_graphemes(text, oracle);
    return prompt;
}

std::string_view to_string(TypingJudgement judgement) noexcept {
    switch (judgement) {
    case TypingJudgement::None:
        return "none";
    case TypingJudgement::Match:
        return "match";
    case TypingJudgement::MatchLenient:
        return "match-lenient";
    case TypingJudgement::Mismatch:
        return "mismatch";
    case TypingJudgement::Skipped:
        return "skipped";
    case TypingJudgement::IgnoredWhitespace:
        return "ignored-whitespace";
    case TypingJudgement::PromptComplete:
        return "prompt-complete";
    }
    return "unknown";
}

void TypingCursor::reset(const TypingPrompt& prompt) noexcept {
    prompt_ = &prompt;
    cursor_ = 0;
    hit_count_ = 0;
    miss_count_ = 0;
    skip_count_ = 0;
    lenient_match_count_ = 0;
    combo_ = 0;
    max_combo_ = 0;
    last_result_ = TypingJudgementResult{};

    // Cache the index where the trailing-whitespace tail begins. For prompts
    // that do not end in whitespace this equals the prompt size, making the
    // ignore_trailing_whitespace branch a no-op without per-call scans.
    effective_finish_index_ = prompt.graphemes.size();
    while (effective_finish_index_ > 0 &&
           grapheme_is_ascii_whitespace(prompt.graphemes[effective_finish_index_ - 1])) {
        --effective_finish_index_;
    }
}

const TypingPrompt* TypingCursor::prompt() const noexcept {
    return prompt_;
}

std::size_t TypingCursor::cursor() const noexcept {
    return cursor_;
}

std::size_t TypingCursor::length() const noexcept {
    return prompt_ != nullptr ? prompt_->graphemes.size() : 0;
}

std::size_t TypingCursor::remaining() const noexcept {
    const std::size_t total = length();
    return cursor_ >= total ? 0 : total - cursor_;
}

bool TypingCursor::finished() const noexcept {
    return prompt_ == nullptr || cursor_ >= prompt_->graphemes.size();
}

std::uint32_t TypingCursor::hit_count() const noexcept {
    return hit_count_;
}

std::uint32_t TypingCursor::miss_count() const noexcept {
    return miss_count_;
}

std::uint32_t TypingCursor::skip_count() const noexcept {
    return skip_count_;
}

std::uint32_t TypingCursor::lenient_match_count() const noexcept {
    return lenient_match_count_;
}

std::uint32_t TypingCursor::combo() const noexcept {
    return combo_;
}

std::uint32_t TypingCursor::max_combo() const noexcept {
    return max_combo_;
}

const TypingGrapheme* TypingCursor::expected() const noexcept {
    if (finished()) {
        return nullptr;
    }
    return &prompt_->graphemes[cursor_];
}

const TypingJudgementResult& TypingCursor::last_result() const noexcept {
    return last_result_;
}

bool TypingCursor::graphemes_equal(
    const TypingGrapheme& expected,
    const TypingGrapheme& observed,
    const TypingLeniency& leniency,
    bool& out_lenient) const {
    out_lenient = false;
    if (expected == observed) {
        return true;
    }
    if (leniency.case_insensitive && ascii_equals_ignore_case(expected, observed)) {
        out_lenient = true;
        return true;
    }
    return false;
}

TypingJudgementResult TypingCursor::accept_grapheme(
    const TypingGrapheme& grapheme,
    const TypingLeniency& leniency) {
    TypingJudgementResult result{};
    result.observed_grapheme = grapheme;

    if (finished()) {
        result.judgement = TypingJudgement::PromptComplete;
        last_result_ = result;
        return result;
    }

    const TypingGrapheme& expected = prompt_->graphemes[cursor_];
    result.prompt_index = cursor_;
    result.expected_grapheme = expected;

    const bool whitespace_observed = grapheme_is_ascii_whitespace(grapheme);
    const bool whitespace_expected = grapheme_is_ascii_whitespace(expected);

    if (whitespace_observed && cursor_ == 0 && leniency.ignore_leading_whitespace && !whitespace_expected) {
        result.judgement = TypingJudgement::IgnoredWhitespace;
        last_result_ = result;
        return result;
    }
    if (whitespace_observed && whitespace_expected && leniency.collapse_whitespace_runs &&
        cursor_ > 0 && grapheme_is_ascii_whitespace(prompt_->graphemes[cursor_ - 1])) {
        // Consume the input whitespace without advancing — the prompt already
        // satisfied this whitespace run on the previous grapheme.
        result.judgement = TypingJudgement::IgnoredWhitespace;
        last_result_ = result;
        return result;
    }

    bool lenient = false;
    if (graphemes_equal(expected, grapheme, leniency, lenient)) {
        result.judgement = lenient ? TypingJudgement::MatchLenient : TypingJudgement::Match;
        result.advanced_cursor = true;
        result.advances_combo = true;
        ++cursor_;
        ++hit_count_;
        if (lenient) {
            ++lenient_match_count_;
        }
        ++combo_;
        max_combo_ = std::max(max_combo_, combo_);

        // ignore_trailing_whitespace: if the cursor has reached the
        // pre-cached trailing-whitespace tail, snap past it so the prompt
        // is treated as complete. The remaining trailing graphemes are
        // silently fast-forwarded; they are neither hits nor misses.
        if (leniency.ignore_trailing_whitespace &&
            cursor_ < prompt_->graphemes.size() &&
            cursor_ >= effective_finish_index_) {
            cursor_ = prompt_->graphemes.size();
        }
        last_result_ = result;
        return result;
    }

    result.judgement = TypingJudgement::Mismatch;
    ++miss_count_;
    if (!leniency.preserve_combo_on_mismatch) {
        combo_ = 0;
        result.resets_combo = true;
    }
    if (leniency.skip_on_mismatch) {
        result.judgement = TypingJudgement::Skipped;
        result.advanced_cursor = true;
        ++cursor_;
        ++skip_count_;
    }
    last_result_ = result;
    return result;
}

TypingJudgementResult TypingCursor::accept(std::string_view utf8_text, const TypingLeniency& leniency) {
    if (utf8_text.empty()) {
        last_result_ = TypingJudgementResult{};
        return last_result_;
    }
    const std::vector<TypingGrapheme> incoming = decode_typing_graphemes(utf8_text, {});
    if (incoming.empty()) {
        last_result_ = TypingJudgementResult{};
        return last_result_;
    }
    TypingJudgementResult last{};
    for (const TypingGrapheme& grapheme : incoming) {
        last = accept_grapheme(grapheme, leniency);
        if (last.judgement == TypingJudgement::PromptComplete) {
            break;
        }
    }
    return last;
}

void TypingAnalytics::reset() noexcept {
    per_grapheme_.clear();
    summary_ = TypingAnalyticsSummary{};
}

void TypingAnalytics::record(const TypingJudgementResult& result) {
    if (result.judgement == TypingJudgement::None ||
        result.judgement == TypingJudgement::PromptComplete) {
        return;
    }

    ++summary_.total_judgements;
    switch (result.judgement) {
    case TypingJudgement::Match:
        ++summary_.total_hits;
        break;
    case TypingJudgement::MatchLenient:
        ++summary_.total_hits;
        ++summary_.total_lenient_hits;
        break;
    case TypingJudgement::Mismatch:
        ++summary_.total_misses;
        break;
    case TypingJudgement::Skipped:
        ++summary_.total_misses;
        ++summary_.total_skips;
        break;
    case TypingJudgement::IgnoredWhitespace:
        ++summary_.total_ignored_whitespace;
        break;
    default:
        break;
    }

    if (!result.expected_grapheme.empty() &&
        result.judgement != TypingJudgement::IgnoredWhitespace) {
        TypingGraphemeStats& stats = per_grapheme_[result.expected_grapheme];
        ++stats.presented;
        switch (result.judgement) {
        case TypingJudgement::Match:
            ++stats.hit;
            break;
        case TypingJudgement::MatchLenient:
            ++stats.hit;
            ++stats.lenient_hit;
            break;
        case TypingJudgement::Mismatch:
            ++stats.miss;
            break;
        case TypingJudgement::Skipped:
            ++stats.miss;
            ++stats.skipped;
            break;
        default:
            break;
        }
    }

    summary_.unique_grapheme_count = per_grapheme_.size();
    if (summary_.total_judgements > 0) {
        const double judged = static_cast<double>(summary_.total_judgements - summary_.total_ignored_whitespace);
        summary_.accuracy_ratio = judged > 0.0 ? static_cast<double>(summary_.total_hits) / judged : 0.0;
    }
}

void TypingAnalytics::record_prompt_completion() noexcept {
    ++summary_.prompt_complete_count;
}

const TypingGraphemeStats* TypingAnalytics::find_stats(const TypingGrapheme& grapheme) const noexcept {
    const auto it = per_grapheme_.find(grapheme);
    return it == per_grapheme_.end() ? nullptr : &it->second;
}

TypingAnalyticsSummary TypingAnalytics::summary() const noexcept {
    return summary_;
}

const std::unordered_map<TypingGrapheme, TypingGraphemeStats>& TypingAnalytics::per_grapheme() const noexcept {
    return per_grapheme_;
}

} // namespace reaktio::gameplay
