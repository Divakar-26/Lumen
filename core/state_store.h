#ifndef LUMEN_CORE_STATE_STORE_H
#define LUMEN_CORE_STATE_STORE_H

#include <cstddef>
#include <cstdint>

namespace Lumen::detail {

struct StateSlot {
    std::uint64_t id = 0;
    std::uint64_t last_seen_frame = 0;
    std::size_t value_size = 0;
    std::uint8_t status = 0;
    bool pinned = false;
};

class StateStore {
public:
    StateStore() noexcept = default;
    ~StateStore();

    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;

    bool Initialize(std::size_t requested_capacity, std::size_t max_value_bytes) noexcept;
    void BeginFrame(std::uint64_t frame_index) noexcept;
    void Sweep(std::uint32_t ttl_frames, std::uint64_t& evictions) noexcept;
    void* Get(std::uint64_t id, std::size_t size, std::size_t alignment,
              const void* initial_value, std::uint64_t& hits, std::uint64_t& misses,
              std::uint64_t& capacity_exhaustions) noexcept;
    void Pin(std::uint64_t id, bool pinned) noexcept;

    std::size_t EntryCount() const noexcept { return entry_count_; }
    std::size_t Capacity() const noexcept { return capacity_; }
    std::size_t MaxValueBytes() const noexcept { return max_value_bytes_; }

private:
    static constexpr std::uint8_t Empty = 0;
    static constexpr std::uint8_t Occupied = 1;
    static constexpr std::uint8_t Tombstone = 2;

    std::size_t FindSlot(std::uint64_t id, bool for_insert) const noexcept;
    void Erase(std::size_t slot) noexcept;

    StateSlot* slots_ = nullptr;
    unsigned char* values_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    std::size_t max_value_bytes_ = 0;
    std::size_t value_stride_ = 0;
    std::size_t entry_count_ = 0;
    std::uint64_t frame_index_ = 0;
};

} // namespace Lumen::detail

#endif
