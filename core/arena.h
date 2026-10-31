#ifndef LUMEN_CORE_ARENA_H
#define LUMEN_CORE_ARENA_H

#include <cstddef>
#include <cstdint>

namespace Lumen::detail {

class FrameArena {
public:
    FrameArena() noexcept = default;
    ~FrameArena();

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    bool Initialize(std::size_t capacity) noexcept;
    void Reset() noexcept;
    void* Allocate(std::size_t size, std::size_t alignment) noexcept;

    std::size_t Capacity() const noexcept { return capacity_; }
    std::size_t Used() const noexcept { return offset_; }
    std::size_t HighWater() const noexcept { return high_water_; }
    std::uint64_t FailedAllocations() const noexcept { return failed_allocations_; }

private:
    unsigned char* memory_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t offset_ = 0;
    std::size_t high_water_ = 0;
    std::uint64_t failed_allocations_ = 0;
};

} // namespace Lumen::detail

#endif
