#include "state_store.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace Lumen::detail {
namespace {

std::size_t NextPowerOfTwo(std::size_t value) noexcept {
    if (value < 16) {
        return 16;
    }
    --value;
    for (std::size_t shift = 1; shift < sizeof(value) * 8; shift <<= 1) {
        value |= value >> shift;
    }
    if (value == std::numeric_limits<std::size_t>::max()) {
        return 0;
    }
    return value + 1;
}

bool MultiplyOverflows(std::size_t left, std::size_t right) noexcept {
    return right != 0 && left > std::numeric_limits<std::size_t>::max() / right;
}

} // namespace

StateStore::~StateStore() {
    std::free(values_);
    std::free(slots_);
}

bool StateStore::Initialize(std::size_t requested_capacity, std::size_t max_value_bytes) noexcept {
    const std::size_t capacity = NextPowerOfTwo(requested_capacity);
    if (capacity == 0 || max_value_bytes == 0) {
        return false;
    }
    const std::size_t alignment = alignof(std::max_align_t);
    if (max_value_bytes > std::numeric_limits<std::size_t>::max() - (alignment - 1)) {
        return false;
    }
    const std::size_t stride = (max_value_bytes + alignment - 1) & ~(alignment - 1);
    if (MultiplyOverflows(capacity, stride)) {
        return false;
    }

    auto* slots = static_cast<StateSlot*>(std::calloc(capacity, sizeof(StateSlot)));
    auto* values = static_cast<unsigned char*>(std::malloc(capacity * stride));
    if (slots == nullptr || values == nullptr) {
        std::free(values);
        std::free(slots);
        return false;
    }

    std::free(values_);
    std::free(slots_);
    slots_ = slots;
    values_ = values;
    capacity_ = capacity;
    mask_ = capacity - 1;
    max_value_bytes_ = max_value_bytes;
    value_stride_ = stride;
    entry_count_ = 0;
    frame_index_ = 0;
    return true;
}

void StateStore::BeginFrame(std::uint64_t frame_index) noexcept {
    frame_index_ = frame_index;
}

std::size_t StateStore::FindSlot(std::uint64_t id, bool for_insert) const noexcept {
    if (capacity_ == 0 || id == 0) {
        return capacity_;
    }

    std::size_t index = static_cast<std::size_t>(id) & mask_;
    std::size_t first_tombstone = capacity_;
    for (std::size_t probe = 0; probe < capacity_; ++probe) {
        const StateSlot& slot = slots_[index];
        if (slot.status == Empty) {
            if (!for_insert) {
                return capacity_;
            }
            return first_tombstone != capacity_ ? first_tombstone : index;
        }
        if (slot.status == Occupied && slot.id == id) {
            return index;
        }
        if (for_insert && slot.status == Tombstone && first_tombstone == capacity_) {
            first_tombstone = index;
        }
        index = (index + 1) & mask_;
    }
    return for_insert ? first_tombstone : capacity_;
}

void* StateStore::Get(std::uint64_t id, std::size_t size, std::size_t alignment,
                      const void* initial_value, std::uint64_t& hits, std::uint64_t& misses,
                      std::uint64_t& capacity_exhaustions) noexcept {
    if (id == 0 || size == 0 || size > max_value_bytes_ || initial_value == nullptr ||
        alignment == 0 || alignment > alignof(std::max_align_t)) {
        ++capacity_exhaustions;
        return nullptr;
    }

    const std::size_t existing = FindSlot(id, false);
    if (existing != capacity_) {
        StateSlot& slot = slots_[existing];
        if (slot.value_size != size) {
            ++capacity_exhaustions;
            return nullptr;
        }
        slot.last_seen_frame = frame_index_;
        ++hits;
        return values_ + existing * value_stride_;
    }

    ++misses;
    const std::size_t index = FindSlot(id, true);
    if (index == capacity_) {
        ++capacity_exhaustions;
        return nullptr;
    }

    StateSlot& slot = slots_[index];
    slot.id = id;
    slot.last_seen_frame = frame_index_;
    slot.value_size = size;
    slot.status = Occupied;
    slot.pinned = false;
    std::memcpy(values_ + index * value_stride_, initial_value, size);
    ++entry_count_;
    return values_ + index * value_stride_;
}

void StateStore::Erase(std::size_t slot_index) noexcept {
    StateSlot& slot = slots_[slot_index];
    slot.id = 0;
    slot.last_seen_frame = 0;
    slot.value_size = 0;
    slot.pinned = false;
    slot.status = Tombstone;
    if (entry_count_ > 0) {
        --entry_count_;
    }
}

void StateStore::Sweep(std::uint32_t ttl_frames, std::uint64_t& evictions) noexcept {
    for (std::size_t i = 0; i < capacity_; ++i) {
        StateSlot& slot = slots_[i];
        if (slot.status != Occupied || slot.pinned || frame_index_ <= slot.last_seen_frame) {
            continue;
        }
        if (frame_index_ - slot.last_seen_frame > ttl_frames) {
            Erase(i);
            ++evictions;
        }
    }
}

void StateStore::Pin(std::uint64_t id, bool pinned) noexcept {
    const std::size_t index = FindSlot(id, false);
    if (index != capacity_) {
        slots_[index].pinned = pinned;
    }
}

} // namespace Lumen::detail
