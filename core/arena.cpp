#include "arena.h"

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace Lumen::detail {

FrameArena::~FrameArena() {
    std::free(memory_);
}

bool FrameArena::Initialize(std::size_t capacity) noexcept {
    if (capacity == 0) {
        return false;
    }

    auto* memory = static_cast<unsigned char*>(std::malloc(capacity));
    if (memory == nullptr) {
        return false;
    }

    std::free(memory_);
    memory_ = memory;
    capacity_ = capacity;
    offset_ = 0;
    high_water_ = 0;
    failed_allocations_ = 0;
    return true;
}

void FrameArena::Reset() noexcept {
    offset_ = 0;
}

void* FrameArena::Allocate(std::size_t size, std::size_t alignment) noexcept {
    if (memory_ == nullptr || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        ++failed_allocations_;
        return nullptr;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(memory_);
    if (offset_ > std::numeric_limits<std::uintptr_t>::max() - base) {
        ++failed_allocations_;
        return nullptr;
    }

    const std::uintptr_t current = base + offset_;
    if (current > std::numeric_limits<std::uintptr_t>::max() - (alignment - 1)) {
        ++failed_allocations_;
        return nullptr;
    }

    const std::uintptr_t aligned = (current + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1u);
    const std::size_t aligned_offset = static_cast<std::size_t>(aligned - base);
    if (aligned_offset > capacity_ || size > capacity_ - aligned_offset) {
        ++failed_allocations_;
        return nullptr;
    }

    offset_ = aligned_offset + size;
    if (offset_ > high_water_) {
        high_water_ = offset_;
    }
    return memory_ + aligned_offset;
}

} // namespace Lumen::detail
