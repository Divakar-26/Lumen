#ifndef LUMEN_LAYERS_H
#define LUMEN_LAYERS_H

#include "lumen/math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Lumen {

// Fixed z groups are part of the rendering contract; activation order is stable
// within a group. No GPU objects, windows, or heap memory are owned here.
enum class LayerKind : std::uint8_t {
    Base = 0, Floating = 1, Popover = 2, Modal = 3, Toast = 4, Drag = 5, Debug = 6
};

enum class DismissReason : std::uint8_t { OutsideClick, Escape, FocusLoss, Timeout };
enum DismissPolicy : std::uint8_t {
    DismissNone = 0,
    DismissOnOutsideClick = 1u << 0u,
    DismissOnEscape = 1u << 1u,
    DismissOnFocusLoss = 1u << 2u,
    DismissOnTimeout = 1u << 3u
};

constexpr std::uint8_t operator|(DismissPolicy lhs, DismissPolicy rhs) noexcept {
    return static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs);
}

struct LayerEntry {
    std::uint64_t id = 0u;
    LayerKind kind = LayerKind::Floating;
    std::uint8_t dismiss_policy = DismissNone;
    double opened_at = 0.0;
    double timeout_seconds = 0.0; // <= 0 means no timeout
    std::uint64_t sequence = 0u;
};

// Bounded layer registry. It stores metadata only; the host remains responsible
// for declaring, painting and owning the content associated with each layer ID.
class LayerStack {
public:
    static constexpr std::size_t kCapacity = 32u;

    bool Open(std::uint64_t id, LayerKind kind, std::uint8_t dismiss_policy = DismissNone,
              double now_seconds = 0.0, double timeout_seconds = 0.0) noexcept {
        if (id == 0u || !std::isfinite(now_seconds)) return false;
        if (static_cast<unsigned>(kind) > static_cast<unsigned>(LayerKind::Debug)) return false;
        now_seconds = std::max(now_seconds, last_time_);
        last_time_ = now_seconds;
        std::size_t existing = FindIndex(id);
        if (existing != count_ && !RemoveAt(existing)) return false;
        if (count_ >= kCapacity) return false;
        LayerEntry entry{};
        entry.id = id;
        entry.kind = kind;
        entry.dismiss_policy = dismiss_policy;
        entry.opened_at = now_seconds;
        entry.timeout_seconds = std::isfinite(timeout_seconds) ? std::max(0.0, timeout_seconds) : 0.0;
        entry.sequence = ++sequence_;
        entries_[count_++] = entry;
        return true;
    }

    bool Close(std::uint64_t id) noexcept {
        const std::size_t index = FindIndex(id);
        return index != count_ && RemoveAt(index);
    }

    bool CloseTop() noexcept {
        if (count_ == 0u) return false;
        --count_;
        entries_[count_] = {};
        return true;
    }

    bool TryDismissTop(DismissReason reason) noexcept {
        if (count_ == 0u) return false;
        const std::uint8_t required = PolicyFor(reason);
        const LayerEntry& top = entries_[TopActivationIndex()];
        if ((top.dismiss_policy & required) == 0u) return false;
        return Close(top.id);
    }

    // Expires every elapsed timeout in one pass. Time is monotonic, so a clock
    // correction cannot prematurely age newly-opened layers.
    bool NeedsFrame() const noexcept {
        for (std::size_t i = 0u; i < count_; ++i) {
            const LayerEntry& entry = entries_[i];
            if ((entry.dismiss_policy & DismissOnTimeout) != 0u && entry.timeout_seconds > 0.0) return true;
        }
        return false;
    }

    std::size_t Advance(double now_seconds) noexcept {
        if (!std::isfinite(now_seconds)) return 0u;
        now_seconds = std::max(now_seconds, last_time_);
        last_time_ = now_seconds;
        std::size_t expired = 0u;
        for (std::size_t i = 0u; i < count_;) {
            const LayerEntry& entry = entries_[i];
            const bool opted_in = (entry.dismiss_policy & DismissOnTimeout) != 0u;
            const bool elapsed = entry.timeout_seconds > 0.0 &&
                                 now_seconds - entry.opened_at >= entry.timeout_seconds;
            if (opted_in && elapsed) {
                RemoveAt(i);
                ++expired;
            } else {
                ++i;
            }
        }
        return expired;
    }

    const LayerEntry* Top() const noexcept {
        return count_ == 0u ? nullptr : &entries_[TopActivationIndex()];
    }
    const LayerEntry* Find(std::uint64_t id) const noexcept {
        const std::size_t index = FindIndex(id);
        return index == count_ ? nullptr : &entries_[index];
    }
    std::size_t Size() const noexcept { return count_; }
    bool Empty() const noexcept { return count_ == 0u; }

    bool HasModal() const noexcept {
        for (std::size_t i = 0u; i < count_; ++i)
            if (entries_[i].kind == LayerKind::Modal) return true;
        return false;
    }

    // While a modal is open, lower z-groups and older modal layers cannot
    // receive input. Toast/drag/debug layers remain usable above the modal.
    bool BlocksInput(std::uint64_t layer_id) const noexcept {
        const LayerEntry* modal = TopModal();
        if (modal == nullptr) return false;
        const LayerEntry* candidate = Find(layer_id);
        if (candidate == nullptr) return true;
        if (candidate->kind > LayerKind::Modal) return false;
        if (candidate->kind < LayerKind::Modal) return true;
        return candidate->id != modal->id;
    }

    const LayerEntry* TopModal() const noexcept {
        const LayerEntry* best = nullptr;
        for (std::size_t i = 0u; i < count_; ++i) {
            const LayerEntry& entry = entries_[i];
            if (entry.kind == LayerKind::Modal && (best == nullptr || entry.sequence > best->sequence)) best = &entry;
        }
        return best;
    }

    // Copies entries in paint order (z group, then opening sequence). Returns
    // number copied, bounded by out_capacity; pass nullptr/0 to query size.
    std::size_t CopyPaintOrder(LayerEntry* out, std::size_t out_capacity) const noexcept {
        if (out == nullptr || out_capacity == 0u) return count_;
        std::size_t indices[kCapacity]{};
        for (std::size_t i = 0u; i < count_; ++i) indices[i] = i;
        for (std::size_t i = 1u; i < count_; ++i) {
            const std::size_t key = indices[i];
            std::size_t j = i;
            while (j > 0u && ComesBefore(entries_[key], entries_[indices[j - 1u]])) {
                indices[j] = indices[j - 1u];
                --j;
            }
            indices[j] = key;
        }
        const std::size_t copied = std::min(count_, out_capacity);
        for (std::size_t i = 0u; i < copied; ++i) out[i] = entries_[indices[i]];
        return copied;
    }

private:
    static std::uint8_t PolicyFor(DismissReason reason) noexcept {
        switch (reason) {
            case DismissReason::OutsideClick: return DismissOnOutsideClick;
            case DismissReason::Escape: return DismissOnEscape;
            case DismissReason::FocusLoss: return DismissOnFocusLoss;
            case DismissReason::Timeout: return DismissOnTimeout;
        }
        return DismissNone;
    }
    static bool ComesBefore(const LayerEntry& lhs, const LayerEntry& rhs) noexcept {
        if (lhs.kind != rhs.kind) return lhs.kind < rhs.kind;
        return lhs.sequence < rhs.sequence;
    }
    std::size_t FindIndex(std::uint64_t id) const noexcept {
        for (std::size_t i = 0u; i < count_; ++i) if (entries_[i].id == id) return i;
        return count_;
    }
    std::size_t TopActivationIndex() const noexcept {
        std::size_t best = 0u;
        for (std::size_t i = 1u; i < count_; ++i)
            if (entries_[i].sequence > entries_[best].sequence) best = i;
        return best;
    }
    bool RemoveAt(std::size_t index) noexcept {
        if (index >= count_) return false;
        for (std::size_t i = index + 1u; i < count_; ++i) entries_[i - 1u] = entries_[i];
        --count_;
        entries_[count_] = {};
        return true;
    }

    LayerEntry entries_[kCapacity]{};
    std::size_t count_ = 0u;
    std::uint64_t sequence_ = 0u;
    double last_time_ = 0.0;
};

enum class Placement : std::uint8_t { Auto, AboveAnchor, BelowAnchor, Leading, Trailing };
enum class PlacementAlign : std::uint8_t { Start, Center, End };

struct AnchorPlacementOptions {
    Rect viewport{};
    Insets safe_area{};
    Rect anchor{};
    Vec2 overlay_size{};
    Placement placement = Placement::Auto;
    PlacementAlign align = PlacementAlign::Start;
    Vec2 offset{};
    bool flip = true;
    bool clamp_to_safe_area = true;
};

struct AnchorPlacementResult {
    Rect rect{};
    Placement placement = Placement::BelowAnchor;
    bool flipped = false;
    bool clamped = false;
};

// Pure geometry helper: suitable for deterministic tests, and usable by a host
// layer painter without coupling the core to a window or renderer.
inline bool ResolveAnchorPlacement(const AnchorPlacementOptions& options,
                                   AnchorPlacementResult* result) noexcept {
    if (result == nullptr || !IsFinite(options.viewport.min) || !IsFinite(options.viewport.max) ||
        !IsFinite(options.anchor.min) || !IsFinite(options.anchor.max) ||
        !IsFinite(options.overlay_size) || !IsFinite(options.offset) ||
        options.viewport.IsEmpty() || options.anchor.IsEmpty() ||
        options.overlay_size.x <= 0.0f || options.overlay_size.y <= 0.0f) return false;

    const auto clean = [](float v) noexcept { return std::isfinite(v) ? std::max(0.0f, v) : 0.0f; };
    const float left = options.viewport.min.x + clean(options.safe_area.left);
    const float top = options.viewport.min.y + clean(options.safe_area.top);
    const float right = options.viewport.max.x - clean(options.safe_area.right);
    const float bottom = options.viewport.max.y - clean(options.safe_area.bottom);
    if (right <= left || bottom <= top) return false;
    const Rect safe{left, top, right - left, bottom - top};
    const Vec2 size = options.overlay_size;

    auto available = [&](Placement p) noexcept -> float {
        switch (p) {
            case Placement::AboveAnchor: return options.anchor.min.y - safe.min.y;
            case Placement::BelowAnchor: return safe.max.y - options.anchor.max.y;
            case Placement::Leading: return options.anchor.min.x - safe.min.x;
            case Placement::Trailing: return safe.max.x - options.anchor.max.x;
            case Placement::Auto: break;
        }
        return 0.0f;
    };
    auto required = [&](Placement p) noexcept -> float {
        return (p == Placement::AboveAnchor || p == Placement::BelowAnchor) ? size.y : size.x;
    };
    Placement actual = options.placement;
    if (actual == Placement::Auto) {
        const Placement candidates[] = {Placement::BelowAnchor, Placement::AboveAnchor, Placement::Trailing, Placement::Leading};
        actual = candidates[0];
        float best = available(actual);
        for (std::size_t i = 0u; i < 4u; ++i) {
            const float space = available(candidates[i]);
            if (space >= required(candidates[i])) { actual = candidates[i]; break; }
            if (space > best) { best = space; actual = candidates[i]; }
        }
    }
    const auto opposite = [](Placement p) noexcept {
        switch (p) {
            case Placement::AboveAnchor: return Placement::BelowAnchor;
            case Placement::BelowAnchor: return Placement::AboveAnchor;
            case Placement::Leading: return Placement::Trailing;
            case Placement::Trailing: return Placement::Leading;
            case Placement::Auto: return Placement::BelowAnchor;
        }
        return Placement::BelowAnchor;
    };
    bool flipped = false;
    if (options.flip && actual != Placement::Auto && available(actual) < required(actual) &&
        available(opposite(actual)) > available(actual)) {
        actual = opposite(actual);
        flipped = true;
    }

    float x = 0.0f, y = 0.0f;
    if (actual == Placement::AboveAnchor || actual == Placement::BelowAnchor) {
        if (options.align == PlacementAlign::Start) x = options.anchor.min.x;
        else if (options.align == PlacementAlign::Center) x = options.anchor.Center().x - size.x * 0.5f;
        else x = options.anchor.max.x - size.x;
        y = actual == Placement::BelowAnchor ? options.anchor.max.y + options.offset.y
                                       : options.anchor.min.y - size.y - options.offset.y;
        x += options.offset.x;
    } else {
        if (options.align == PlacementAlign::Start) y = options.anchor.min.y;
        else if (options.align == PlacementAlign::Center) y = options.anchor.Center().y - size.y * 0.5f;
        else y = options.anchor.max.y - size.y;
        x = actual == Placement::Trailing ? options.anchor.max.x + options.offset.x
                                          : options.anchor.min.x - size.x - options.offset.x;
        y += options.offset.y;
    }
    const float original_x = x, original_y = y;
    if (options.clamp_to_safe_area) {
        const float max_x = std::max(safe.min.x, safe.max.x - size.x);
        const float max_y = std::max(safe.min.y, safe.max.y - size.y);
        x = Clamp(x, safe.min.x, max_x);
        y = Clamp(y, safe.min.y, max_y);
    }
    result->rect = Rect{x, y, size.x, size.y};
    result->placement = actual;
    result->flipped = flipped;
    result->clamped = std::fabs(x - original_x) > 1.0e-4f || std::fabs(y - original_y) > 1.0e-4f;
    return true;
}

} // namespace Lumen

#endif
