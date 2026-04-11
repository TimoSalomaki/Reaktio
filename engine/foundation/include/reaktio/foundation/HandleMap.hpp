#pragma once

#include "reaktio/foundation/StrongId.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace reaktio::foundation {

template <typename Tag, typename Value>
class HandleMap {
  public:
    using handle_type = StrongId<Tag, std::uint64_t>;

    private:
        struct ControlBlock;

    public:

    HandleMap()
        : control_(std::make_shared<ControlBlock>()) {
        control_->owner = this;
    }

    ~HandleMap() {
        if (control_) {
            control_->owner = nullptr;
        }
    }

    HandleMap(const HandleMap&) = delete;
    HandleMap& operator=(const HandleMap&) = delete;

    HandleMap(HandleMap&& other) noexcept
        : slots_(std::move(other.slots_)),
          free_indices_(std::move(other.free_indices_)),
          size_(other.size_),
          revision_(other.revision_),
          control_(std::move(other.control_)) {
        if (!control_) {
            control_ = std::make_shared<ControlBlock>();
        }
        control_->owner = this;
        other.reset_moved_from_state();
    }

    HandleMap& operator=(HandleMap&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (control_) {
            control_->owner = nullptr;
        }

        slots_ = std::move(other.slots_);
        free_indices_ = std::move(other.free_indices_);
        size_ = other.size_;
        revision_ = other.revision_;
        control_ = std::move(other.control_);
        if (!control_) {
            control_ = std::make_shared<ControlBlock>();
        }
        control_->owner = this;
        other.reset_moved_from_state();
        return *this;
    }

    template <bool IsConst>
    class BorrowedEntry {
      public:
        using map_type = std::conditional_t<IsConst, const HandleMap, HandleMap>;
        using value_type = std::conditional_t<IsConst, const Value, Value>;

        constexpr BorrowedEntry() noexcept = default;
        BorrowedEntry(std::weak_ptr<const ControlBlock> control, handle_type handle) noexcept
            : control_(std::move(control)), handle_(handle) {}

        [[nodiscard]] value_type* get() const noexcept {
            const std::shared_ptr<const ControlBlock> control = control_.lock();
            if (control == nullptr || control->owner == nullptr) {
                return nullptr;
            }

            if constexpr (IsConst) {
                return control->owner->try_get(handle_);
            } else {
                return const_cast<HandleMap*>(control->owner)->try_get(handle_);
            }
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return get() != nullptr;
        }

        [[nodiscard]] handle_type handle() const noexcept {
            return handle_;
        }

        [[nodiscard]] value_type* operator->() const noexcept {
            return get();
        }

        [[nodiscard]] value_type& operator*() const {
            value_type* value = get();
            assert(value != nullptr);
            return *value;
        }

      private:
                std::weak_ptr<const ControlBlock> control_;
        handle_type handle_{};
    };

    using Borrowed = BorrowedEntry<false>;
    using ConstBorrowed = BorrowedEntry<true>;

    template <typename... Args>
    [[nodiscard]] handle_type emplace(Args&&... args) {
        std::uint32_t index = 0;
        if (free_indices_.empty()) {
            if (slots_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return {};
            }

            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back(Slot{});
        } else {
            index = free_indices_.back();
            free_indices_.pop_back();
        }

        Slot& slot = slots_[index];
        if (slot.generation == 0u) {
            slot.generation = 1u;
        }

        slot.value.emplace(std::forward<Args>(args)...);
        const handle_type handle = make_handle(index, slot.generation);
        ++size_;
        ++revision_;
        return handle;
    }

    template <typename... Args>
    [[nodiscard]] handle_type replace(handle_type handle, Args&&... args) {
        Slot* slot = try_get_slot(handle);
        if (slot == nullptr) {
            return {};
        }

        const std::uint32_t index = slot_index(handle);
        slot->generation = next_generation(slot->generation);
        slot->value.emplace(std::forward<Args>(args)...);
        const handle_type replacement = make_handle(index, slot->generation);
        ++revision_;
        return replacement;
    }

    bool erase(handle_type handle) noexcept {
        Slot* slot = try_get_slot(handle);
        if (slot == nullptr) {
            return false;
        }

        const std::uint32_t index = slot_index(handle);
        slot->value.reset();
        slot->generation = next_generation(slot->generation);
        free_indices_.push_back(index);
        --size_;
        ++revision_;
        return true;
    }

    void clear() noexcept {
        slots_.clear();
        free_indices_.clear();
        size_ = 0;
        revision_ = 0;
    }

    [[nodiscard]] bool contains(handle_type handle) const noexcept {
        return try_get(handle) != nullptr;
    }

    [[nodiscard]] Value* try_get(handle_type handle) noexcept {
        Slot* slot = try_get_slot(handle);
        return slot != nullptr ? &(*slot->value) : nullptr;
    }

    [[nodiscard]] const Value* try_get(handle_type handle) const noexcept {
        const Slot* slot = try_get_slot(handle);
        return slot != nullptr ? &(*slot->value) : nullptr;
    }

    [[nodiscard]] Borrowed borrow(handle_type handle) noexcept {
        return Borrowed{control_, handle};
    }

    [[nodiscard]] ConstBorrowed borrow(handle_type handle) const noexcept {
        return ConstBorrowed{control_, handle};
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

  private:
        struct ControlBlock {
        const HandleMap* owner{};
    };

    struct Slot {
        std::optional<Value> value;
        std::uint32_t generation{1};
    };

    void reset_moved_from_state() {
        slots_.clear();
        free_indices_.clear();
        size_ = 0;
        revision_ = 0;
        control_ = std::make_shared<ControlBlock>();
        control_->owner = this;
    }

    [[nodiscard]] static handle_type make_handle(std::uint32_t slot_index, std::uint32_t generation) noexcept {
        return handle_type{(static_cast<std::uint64_t>(generation) << 32u) |
                           (static_cast<std::uint64_t>(slot_index) + 1u)};
    }

    [[nodiscard]] static std::uint32_t slot_index(handle_type handle) noexcept {
        return handle.valid()
            ? static_cast<std::uint32_t>((handle.value() & 0xffffffffull) - 1u)
            : std::numeric_limits<std::uint32_t>::max();
    }

    [[nodiscard]] static std::uint32_t generation(handle_type handle) noexcept {
        return static_cast<std::uint32_t>(handle.value() >> 32u);
    }

    [[nodiscard]] static std::uint32_t next_generation(std::uint32_t generation_value) noexcept {
        return generation_value == std::numeric_limits<std::uint32_t>::max() ? 1u : generation_value + 1u;
    }

    [[nodiscard]] Slot* try_get_slot(handle_type handle) noexcept {
        if (!handle.valid()) {
            return nullptr;
        }

        const std::uint32_t index = slot_index(handle);
        if (index >= slots_.size()) {
            return nullptr;
        }

        Slot& slot = slots_[index];
        if (!slot.value.has_value() || slot.generation != generation(handle)) {
            return nullptr;
        }

        return &slot;
    }

    [[nodiscard]] const Slot* try_get_slot(handle_type handle) const noexcept {
        if (!handle.valid()) {
            return nullptr;
        }

        const std::uint32_t index = slot_index(handle);
        if (index >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[index];
        if (!slot.value.has_value() || slot.generation != generation(handle)) {
            return nullptr;
        }

        return &slot;
    }

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t size_{};
    std::uint64_t revision_{};
    std::shared_ptr<ControlBlock> control_;
};

} // namespace reaktio::foundation