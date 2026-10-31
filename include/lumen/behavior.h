#ifndef LUMEN_BEHAVIOR_H
#define LUMEN_BEHAVIOR_H

#include "lumen/lumen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Lumen {

// A bounded route stack suitable for UI navigation. Route names are copied into
// the stack, so callers do not need to keep temporary strings alive.
class Router {
public:
    static constexpr std::size_t kMaxDepth = 32u;
    static constexpr std::size_t kNameCapacity = 64u;

    bool Push(const char* route) noexcept { return Insert(route, false); }
    bool Replace(const char* route) noexcept { return Insert(route, true); }

    bool Pop() noexcept {
        if (depth_ <= 1u) return false;
        --depth_;
        routes_[depth_][0] = '\0';
        return true;
    }

    void Reset() noexcept {
        for (std::size_t i = 0; i < depth_; ++i) routes_[i][0] = '\0';
        depth_ = 0u;
    }

    const char* Current() const noexcept { return depth_ == 0u ? nullptr : routes_[depth_ - 1u]; }
    const char* Previous() const noexcept { return depth_ < 2u ? nullptr : routes_[depth_ - 2u]; }
    std::size_t Depth() const noexcept { return depth_; }
    bool CanPop() const noexcept { return depth_ > 1u; }

private:
    bool Insert(const char* route, bool replace) noexcept {
        if (route == nullptr || route[0] == '\0') return false;
        std::size_t length = 0u;
        while (length < kNameCapacity && route[length] != '\0') ++length;
        if (length == 0u || length >= kNameCapacity) return false;
        if (replace) {
            if (depth_ == 0u) return false;
            // memmove also makes Replace(Current()) well-defined when source and destination alias.
            std::memmove(routes_[depth_ - 1u], route, length + 1u);
            return true;
        }
        if (depth_ >= kMaxDepth) return false;
        std::memmove(routes_[depth_], route, length + 1u);
        ++depth_;
        return true;
    }

    char routes_[kMaxDepth][kNameCapacity]{};
    std::size_t depth_ = 0u;
};

enum class PresencePhase : std::uint8_t { Absent, Entering, Present, Exiting };

// Interruptible enter/exit state. Opacity starts from its current value when
// retargeted, so reversing an exit does not jump back to the start value.
class Presence {
public:
    explicit Presence(bool initially_present = false) noexcept
        : phase_(initially_present ? PresencePhase::Present : PresencePhase::Absent),
          opacity_(initially_present ? 1.0f : 0.0f), target_(opacity_) {}

    void SetPresent(bool present, double now_seconds, double duration_seconds,
                    Curve curve = Curve::Standard()) noexcept {
        if (!std::isfinite(now_seconds)) now_seconds = last_time_;
        now_seconds = std::max(now_seconds, last_time_);
        Advance(now_seconds);
        const float target = present ? 1.0f : 0.0f;
        if (target == target_) return;
        target_ = target;
        start_ = opacity_;
        start_time_ = now_seconds;
        duration_ = std::isfinite(duration_seconds) ? std::max(0.0, duration_seconds) : 0.0;
        curve_ = curve;
        if (duration_ <= 0.0 || std::fabs(target_ - start_) <= 1.0e-6f) {
            opacity_ = target_;
            phase_ = target_ > 0.5f ? PresencePhase::Present : PresencePhase::Absent;
            return;
        }
        phase_ = target_ > 0.5f ? PresencePhase::Entering : PresencePhase::Exiting;
    }

    bool Advance(double now_seconds) noexcept {
        if (!std::isfinite(now_seconds)) return IsAnimating();
        now_seconds = std::max(now_seconds, last_time_);
        last_time_ = now_seconds;
        if (!IsAnimating()) return false;
        const double elapsed = std::max(0.0, now_seconds - start_time_);
        const float progress = duration_ <= 0.0 ? 1.0f : static_cast<float>(std::min(1.0, elapsed / duration_));
        const float curve_value = curve_.Evaluate(progress);
        // A custom curve must never poison persistent visual state with NaN/Inf.
        // Fall back to linear progress while preserving finite overshoot policy
        // for the general motion system (Presence itself is constrained to [0,1]).
        const float eased = std::isfinite(curve_value) ? Clamp(curve_value, 0.0f, 1.0f) : progress;
        opacity_ = start_ + (target_ - start_) * eased;
        if (progress >= 1.0f) {
            opacity_ = target_;
            phase_ = target_ > 0.5f ? PresencePhase::Present : PresencePhase::Absent;
        }
        return IsAnimating();
    }

    PresencePhase Phase() const noexcept { return phase_; }
    float Opacity() const noexcept { return Clamp(opacity_, 0.0f, 1.0f); }
    bool IsMounted() const noexcept { return phase_ != PresencePhase::Absent || target_ > 0.5f; }
    bool IsAnimating() const noexcept {
        return phase_ == PresencePhase::Entering || phase_ == PresencePhase::Exiting;
    }
    bool NeedsFrame() const noexcept { return IsAnimating(); }

private:
    PresencePhase phase_ = PresencePhase::Absent;
    float opacity_ = 0.0f;
    float target_ = 0.0f;
    float start_ = 0.0f;
    double start_time_ = 0.0;
    double duration_ = 0.0;
    double last_time_ = 0.0;
    Curve curve_ = Curve::Standard();
};

} // namespace Lumen

#endif
