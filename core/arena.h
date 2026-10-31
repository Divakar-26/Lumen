#ifndef LUMEN_CORE_ARENA_H
#define LUMEN_CORE_ARENA_H

#include <cstddef>
#include <cstdint>

namespace Lumen::detail {

// Bump allocator over a list of chunks. The first chunk is reserved up front; when a frame needs
// more, a new chunk is allocated (at least chunk_bytes) until max_bytes are reserved. Chunks are kept
// across frames, so once the largest frame has been seen there are no further allocations.
class FrameArena {
public:
    static constexpr std::size_t kMaxChunks = 32u;

    FrameArena() noexcept = default;
    ~FrameArena();

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    bool Initialize(std::size_t chunk_bytes, std::size_t max_bytes) noexcept;
    void Reset() noexcept;
    void* Allocate(std::size_t size, std::size_t alignment) noexcept;

    std::size_t Reserved() const noexcept { return reserved_; }
    std::size_t Used() const noexcept;
    std::size_t HighWater() const noexcept { return high_water_; }
    std::uint64_t FailedAllocations() const noexcept { return failed_allocations_; }

private:
    struct Chunk {
        unsigned char* memory = nullptr;
        std::size_t capacity = 0;
        std::size_t offset = 0;
    };

    void Release() noexcept;
    static void* AllocateIn(Chunk& chunk, std::size_t size, std::size_t alignment) noexcept;

    Chunk chunks_[kMaxChunks]{};
    std::size_t chunk_count_ = 0;
    std::size_t current_ = 0;
    std::size_t chunk_bytes_ = 0;
    std::size_t max_bytes_ = 0;
    std::size_t reserved_ = 0;
    std::size_t high_water_ = 0;
    std::uint64_t failed_allocations_ = 0;
};

} // namespace Lumen::detail

#endif
