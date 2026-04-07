#include "reaktio/foundation/DeterministicRandom.hpp"

#include <algorithm>
#include <limits>

namespace reaktio::foundation {

namespace {

std::uint64_t fnv1a64(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const char character : text) {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }

    return hash;
}

std::uint64_t mix_bits(std::uint64_t state) noexcept {
    std::uint64_t value = state;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

} // namespace

DeterministicRng::DeterministicRng(std::uint64_t seed) noexcept {
    reseed(seed);
}

void DeterministicRng::reseed(std::uint64_t seed) noexcept {
    state_ = seed;
    initial_seed_ = seed;
    generated_values_ = 0;
}

std::uint64_t DeterministicRng::initial_seed() const noexcept {
    return initial_seed_;
}

std::uint64_t DeterministicRng::generated_values() const noexcept {
    return generated_values_;
}

std::uint64_t DeterministicRng::next_u64() noexcept {
    state_ += 0x9e3779b97f4a7c15ull;
    ++generated_values_;
    return mix_bits(state_);
}

std::uint32_t DeterministicRng::next_u32() noexcept {
    return static_cast<std::uint32_t>(next_u64() >> 32u);
}

std::uint32_t DeterministicRng::next_u32(std::uint32_t min_inclusive, std::uint32_t max_inclusive) noexcept {
    if (min_inclusive > max_inclusive) {
        std::swap(min_inclusive, max_inclusive);
    }

    const std::uint64_t range = static_cast<std::uint64_t>(max_inclusive) - min_inclusive + 1ull;
    const std::uint64_t threshold = (std::numeric_limits<std::uint64_t>::max() - range + 1ull) % range;

    std::uint64_t sample = 0;
    do {
        sample = next_u64();
    } while (sample < threshold);

    return static_cast<std::uint32_t>(min_inclusive + (sample % range));
}

std::int32_t DeterministicRng::next_i32(std::int32_t min_inclusive, std::int32_t max_inclusive) noexcept {
    if (min_inclusive > max_inclusive) {
        std::swap(min_inclusive, max_inclusive);
    }

    const std::uint64_t range = static_cast<std::uint64_t>(
                                     static_cast<std::int64_t>(max_inclusive) -
                                     static_cast<std::int64_t>(min_inclusive)) +
                                 1ull;
    const std::uint64_t threshold = (std::numeric_limits<std::uint64_t>::max() - range + 1ull) % range;

    std::uint64_t sample = 0;
    do {
        sample = next_u64();
    } while (sample < threshold);

    return static_cast<std::int32_t>(static_cast<std::int64_t>(min_inclusive) + static_cast<std::int64_t>(sample % range));
}

double DeterministicRng::next_unit_f64() noexcept {
    constexpr double k_scale = 1.0 / static_cast<double>(1ull << 53u);
    return static_cast<double>(next_u64() >> 11u) * k_scale;
}

float DeterministicRng::next_unit_f32() noexcept {
    constexpr float k_scale = 1.0f / static_cast<float>(1u << 24u);
    return static_cast<float>(next_u64() >> 40u) * k_scale;
}

bool DeterministicRng::next_bool() noexcept {
    return (next_u64() & 1ull) != 0;
}

DeterministicRandomService::DeterministicRandomService(std::uint64_t root_seed) noexcept
    : root_seed_(root_seed) {}

void DeterministicRandomService::reset_streams() noexcept {
    streams_.clear();
}

void DeterministicRandomService::reseed_root(std::uint64_t root_seed) noexcept {
    root_seed_ = root_seed;
    for (StreamEntry& entry : streams_) {
        entry.derived_seed = derive_seed(root_seed_, entry.name);
        entry.generator.reseed(entry.derived_seed);
    }
}

std::uint64_t DeterministicRandomService::root_seed() const noexcept {
    return root_seed_;
}

std::size_t DeterministicRandomService::stream_count() const noexcept {
    return streams_.size();
}

bool DeterministicRandomService::has_stream(std::string_view name) const noexcept {
    return find_entry(name) != nullptr;
}

std::uint64_t DeterministicRandomService::stream_seed(std::string_view name) const noexcept {
    if (const StreamEntry* entry = find_entry(name)) {
        return entry->derived_seed;
    }

    return derive_seed(root_seed_, name);
}

DeterministicRng& DeterministicRandomService::stream(std::string_view name) {
    if (StreamEntry* entry = find_entry(name)) {
        return entry->generator;
    }

    const std::uint64_t derived_seed = derive_seed(root_seed_, name);
    streams_.push_back(StreamEntry{
        .name = std::string(name),
        .derived_seed = derived_seed,
        .generator = DeterministicRng{derived_seed},
    });
    return streams_.back().generator;
}

const DeterministicRng* DeterministicRandomService::find_stream(std::string_view name) const noexcept {
    if (const StreamEntry* entry = find_entry(name)) {
        return &entry->generator;
    }

    return nullptr;
}

std::uint64_t DeterministicRandomService::derive_seed(std::uint64_t root_seed, std::string_view name) noexcept {
    return mix_bits(root_seed ^ fnv1a64(name));
}

DeterministicRandomService::StreamEntry* DeterministicRandomService::find_entry(std::string_view name) noexcept {
    for (StreamEntry& entry : streams_) {
        if (entry.name == name) {
            return &entry;
        }
    }

    return nullptr;
}

const DeterministicRandomService::StreamEntry* DeterministicRandomService::find_entry(std::string_view name) const noexcept {
    for (const StreamEntry& entry : streams_) {
        if (entry.name == name) {
            return &entry;
        }
    }

    return nullptr;
}

} // namespace reaktio::foundation