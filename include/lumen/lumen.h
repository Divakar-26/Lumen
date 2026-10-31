#ifndef LUMEN_LUMEN_H
#define LUMEN_LUMEN_H

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <imgui.h>
#include "lumen/clock.h"
#include "lumen/math.h"
#include "lumen/layers.h"

#define LUMEN_API_VERSION 0x00010001u

namespace Lumen {

struct Context;
class Controller;

struct Config {
    // The frame arena starts with one frame_arena_bytes chunk and grows by further chunks (kept for
    // later frames) up to frame_arena_max_bytes; allocations beyond the cap fail.
    std::size_t frame_arena_bytes = 1024u * 1024u;
    std::size_t frame_arena_max_bytes = 16u * 1024u * 1024u;
    std::size_t state_capacity = 8192u;
    std::size_t max_state_bytes = 256u;
    std::uint32_t state_ttl_frames = 120u;
    std::size_t motion_capacity = 128u;
    const Clock* clock = nullptr;
};

struct Viewport {
    Rect bounds{};
    Insets safe_area{};
    Vec2 scale{1.0f, 1.0f};
};

struct FrameInfo {
    std::uint64_t index = 0;
    float delta_seconds = 0.0f;
    double time_seconds = 0.0;
    std::uint64_t root_id = 0;
    Viewport viewport{};
};

struct DebugStats {
    std::uint64_t frames_started = 0;
    std::uint64_t arena_high_water_bytes = 0;
    std::uint64_t arena_failed_allocations = 0;
    std::uint64_t state_hits = 0;
    std::uint64_t state_misses = 0;
    std::uint64_t state_evictions = 0;
    std::uint64_t state_capacity_exhaustions = 0;
    std::size_t state_entries = 0;
    std::size_t arena_bytes_used = 0;
    std::size_t arena_reserved_bytes = 0;
    std::size_t nodes_built = 0;
    std::size_t nodes_painted = 0;
    std::uint64_t duplicate_node_ids = 0;
    bool in_frame = false;
};

Context* CreateContext(const Config& config = {}) noexcept;
void DestroyContext(Context* context) noexcept;

bool BeginFrame(Context* context, const Viewport& viewport, float delta_seconds) noexcept;
bool EndFrame(Context* context) noexcept;
bool NeedsFrame(const Context* context) noexcept;
void RequestRedraw(Context* context) noexcept;
bool AttachController(Context* context, Controller* controller) noexcept;
bool DetachController(Context* context, Controller* controller) noexcept;
void SetMotionTimeScale(Context* context, float scale) noexcept;
float GetMotionTimeScale(const Context* context) noexcept;
void SetReduceMotion(Context* context, bool enabled) noexcept;
bool IsReduceMotionEnabled(const Context* context) noexcept;

const FrameInfo* GetFrameInfo(const Context* context) noexcept;
DebugStats GetDebugStats(const Context* context) noexcept;
void* FrameAlloc(Context* context, std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

std::uint64_t MakeId(std::uint64_t parent_id, const char* key) noexcept;
std::uint64_t MakeId(std::uint64_t parent_id, std::uint64_t key) noexcept;

void PinState(Context* context, std::uint64_t id, bool pinned = true) noexcept;

namespace detail {
void* GetState(Context* context, std::uint64_t id, std::size_t size, std::size_t alignment,
               const void* initial_value) noexcept;
void MarkImplicitMotionActive(Context* context) noexcept;
}

// State values are stored in a preallocated pool and must be trivially copyable.
template <typename T>
T* State(Context* context, std::uint64_t id, const T& initial = T{}) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "Lumen state must be trivially copyable");
    static_assert(std::is_trivially_destructible<T>::value, "Lumen state must be trivially destructible");
    return static_cast<T*>(detail::GetState(context, id, sizeof(T), alignof(T), &initial));
}

void InjectMousePosition(Context* context, Vec2 position) noexcept;
void InjectMouseButton(Context* context, int button, bool down) noexcept;
void InjectMouseWheel(Context* context, Vec2 wheel) noexcept;
void InjectKey(Context* context, ImGuiKey key, bool down) noexcept;
void InjectText(Context* context, const char* utf8_text) noexcept;
void InjectFocus(Context* context, bool focused) noexcept;

// Host clipboard through the context's ImGui platform handlers. The returned text is owned by the
// host and valid until the next clipboard call.
const char* GetClipboardText(Context* context) noexcept;
void SetClipboardText(Context* context, const char* utf8_text) noexcept;

} // namespace Lumen

#include "lumen/motion.h"
#include "lumen/ui.h"
#include "lumen/behavior.h"

#endif
