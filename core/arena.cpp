#include "arena.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace Lumen::detail {

FrameArena::~FrameArena() {
    Release();
}

void FrameArena::Release() noexcept {
    for (std::size_t i = 0; i < chunk_count_; ++i) std::free(chunks_[i].memory);
    for (Chunk& chunk : chunks_) chunk = {};
    chunk_count_ = 0;
    current_ = 0;
    reserved_ = 0;
}

bool FrameArena::Initialize(std::size_t chunk_bytes, std::size_t max_bytes) noexcept {
    if (chunk_bytes == 0) {
        return false;
    }

    auto* memory = static_cast<unsigned char*>(std::malloc(chunk_bytes));
    if (memory == nullptr) {
        return false;
    }

    Release();
    chunks_[0] = Chunk{memory, chunk_bytes, 0};
    chunk_count_ = 1;
    chunk_bytes_ = chunk_bytes;
    max_bytes_ = std::max(chunk_bytes, max_bytes);
    reserved_ = chunk_bytes;
    high_water_ = 0;
    failed_allocations_ = 0;
    return true;
}

void FrameArena::Reset() noexcept {
    for (std::size_t i = 0; i < chunk_count_; ++i) chunks_[i].offset = 0;
    current_ = 0;
}

std::size_t FrameArena::Used() const noexcept {
    std::size_t used = 0;
    for (std::size_t i = 0; i < chunk_count_ && i <= current_; ++i) used += chunks_[i].offset;
    return used;
}

void* FrameArena::AllocateIn(Chunk& chunk, std::size_t size, std::size_t alignment) noexcept {
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(chunk.memory);
    if (chunk.offset > std::numeric_limits<std::uintptr_t>::max() - base) return nullptr;
    const std::uintptr_t current = base + chunk.offset;
    if (current > std::numeric_limits<std::uintptr_t>::max() - (alignment - 1)) return nullptr;
    const std::uintptr_t aligned = (current + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1u);
    const std::size_t aligned_offset = static_cast<std::size_t>(aligned - base);
    if (aligned_offset > chunk.capacity || size > chunk.capacity - aligned_offset) return nullptr;
    chunk.offset = aligned_offset + size;
    return chunk.memory + aligned_offset;
}

void* FrameArena::Allocate(std::size_t size, std::size_t alignment) noexcept {
    if (chunk_count_ == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        size > std::numeric_limits<std::size_t>::max() - alignment) {
        ++failed_allocations_;
        return nullptr;
    }

    void* result = nullptr;
    // Later chunks retained from earlier frames are reused before anything new is reserved.
    for (std::size_t i = current_; i < chunk_count_ && result == nullptr; ++i) {
        result = AllocateIn(chunks_[i], size, alignment);
        if (result != nullptr) current_ = i;
    }
    if (result == nullptr) {
        const std::size_t needed = std::max(chunk_bytes_, size + alignment - 1);
        if (chunk_count_ == kMaxChunks || needed > max_bytes_ - reserved_) {
            ++failed_allocations_;
            return nullptr;
        }
        auto* memory = static_cast<unsigned char*>(std::malloc(needed));
        if (memory == nullptr) {
            ++failed_allocations_;
            return nullptr;
        }
        chunks_[chunk_count_] = Chunk{memory, needed, 0};
        current_ = chunk_count_++;
        reserved_ += needed;
        result = AllocateIn(chunks_[current_], size, alignment);
    }

    const std::size_t used = Used();
    if (used > high_water_) high_water_ = used;
    return result;
}

} // namespace Lumen::detail
