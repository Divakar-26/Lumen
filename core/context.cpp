#include "context_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <new>

namespace Lumen {
namespace {

Context* g_current_context = nullptr;

bool IsValidConfig(const Config& config) noexcept {
    return config.frame_arena_bytes > 0 && config.state_capacity > 0 && config.max_state_bytes > 0 && config.motion_capacity > 0;
}

bool NormalizeViewport(const Viewport& source, Viewport* destination) noexcept {
    if (destination == nullptr || !IsFinite(source.bounds.min) || !IsFinite(source.bounds.max) ||
        !IsFinite(source.scale) || source.scale.x <= 0.0f || source.scale.y <= 0.0f) return false;
    const float width = source.bounds.Width();
    const float height = source.bounds.Height();
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0.0f || height < 0.0f) return false;

    const auto safe_inset = [](float value) noexcept {
        return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
    };
    Viewport normalized = source;
    normalized.safe_area.top = std::min(safe_inset(source.safe_area.top), height);
    normalized.safe_area.left = std::min(safe_inset(source.safe_area.left), width);
    normalized.safe_area.right = std::min(safe_inset(source.safe_area.right), width - normalized.safe_area.left);
    normalized.safe_area.bottom = std::min(safe_inset(source.safe_area.bottom), height - normalized.safe_area.top);
    *destination = normalized;
    return true;
}

float SafeLerp(float from, float to, float progress) noexcept {
    if (!std::isfinite(from)) from = std::isfinite(to) ? to : 0.0f;
    if (!std::isfinite(to)) to = from;
    return from + (to - from) * progress;
}

Color SafeThemeColor(Color color, Color fallback) noexcept {
    const auto component = [](float value, float fallback_value) noexcept {
        if (!std::isfinite(value)) value = fallback_value;
        if (!std::isfinite(value)) value = 0.0f;
        return Clamp(value, 0.0f, 1.0f);
    };
    color.r = component(color.r, fallback.r);
    color.g = component(color.g, fallback.g);
    color.b = component(color.b, fallback.b);
    color.a = component(color.a, fallback.a);
    return color;
}

Color BlendThemeColor(Color from, Color to, float progress) noexcept {
    from = SafeThemeColor(from, {});
    to = SafeThemeColor(to, from);
    Color value = LerpColorOKLab(from, to, progress);
    if (!std::isfinite(value.r) || !std::isfinite(value.g) ||
        !std::isfinite(value.b) || !std::isfinite(value.a)) {
        value = {SafeLerp(from.r, to.r, progress), SafeLerp(from.g, to.g, progress),
                 SafeLerp(from.b, to.b, progress), SafeLerp(from.a, to.a, progress)};
    }
    value.r = Clamp(value.r, 0.0f, 1.0f);
    value.g = Clamp(value.g, 0.0f, 1.0f);
    value.b = Clamp(value.b, 0.0f, 1.0f);
    value.a = Clamp(value.a, 0.0f, 1.0f);
    return value;
}

Theme BlendTheme(const Theme& from, const Theme& to, float progress) noexcept {
    Theme result = to;
#define LUMEN_BLEND_COLOR(name) result.name = BlendThemeColor(from.name, to.name, progress)
#define LUMEN_BLEND_FLOAT(name) result.name = SafeLerp(from.name, to.name, progress)
    LUMEN_BLEND_COLOR(canvas);
    LUMEN_BLEND_COLOR(surface);
    LUMEN_BLEND_COLOR(surface_alt);
    LUMEN_BLEND_COLOR(text);
    LUMEN_BLEND_COLOR(muted_text);
    LUMEN_BLEND_COLOR(border);
    LUMEN_BLEND_COLOR(accent);
    LUMEN_BLEND_COLOR(accent_hover);
    LUMEN_BLEND_COLOR(accent_pressed);
    LUMEN_BLEND_COLOR(accent_text);
    LUMEN_BLEND_COLOR(success);
    LUMEN_BLEND_COLOR(warning);
    LUMEN_BLEND_COLOR(danger);
    LUMEN_BLEND_COLOR(surface_container_high);
    LUMEN_BLEND_COLOR(outline_variant);
    LUMEN_BLEND_COLOR(primary_container);
    LUMEN_BLEND_COLOR(secondary_container);
    LUMEN_BLEND_COLOR(on_secondary_container);
    LUMEN_BLEND_COLOR(inverse_surface);
    LUMEN_BLEND_COLOR(inverse_on_surface);
    LUMEN_BLEND_COLOR(inverse_primary);
    LUMEN_BLEND_COLOR(inverse_info);
    LUMEN_BLEND_COLOR(inverse_success);
    LUMEN_BLEND_COLOR(inverse_warning);
    LUMEN_BLEND_COLOR(inverse_danger);
    LUMEN_BLEND_COLOR(shadow);
    LUMEN_BLEND_COLOR(scrim);
    LUMEN_BLEND_COLOR(ink);
    LUMEN_BLEND_FLOAT(spacing);
    LUMEN_BLEND_FLOAT(radius);
    LUMEN_BLEND_FLOAT(caption_size);
    LUMEN_BLEND_FLOAT(font_size);
    LUMEN_BLEND_FLOAT(heading_size);
    LUMEN_BLEND_FLOAT(title_size);
    LUMEN_BLEND_FLOAT(button_min_height);
    LUMEN_BLEND_FLOAT(focus_ring_width);
    LUMEN_BLEND_FLOAT(focus_ring_outset);
    LUMEN_BLEND_FLOAT(minimum_hit_target);
    LUMEN_BLEND_FLOAT(scroll_wheel_multiplier);
#undef LUMEN_BLEND_COLOR
#undef LUMEN_BLEND_FLOAT
    result.button_padding = {
        SafeLerp(from.button_padding.top, to.button_padding.top, progress),
        SafeLerp(from.button_padding.right, to.button_padding.right, progress),
        SafeLerp(from.button_padding.bottom, to.button_padding.bottom, progress),
        SafeLerp(from.button_padding.left, to.button_padding.left, progress)};
    return result;
}

void AdvanceThemeTransition(Context* context) noexcept {
    if (context == nullptr || !context->theme_transition_active) return;
    const double elapsed = std::max(0.0, context->motion_time_seconds - context->theme_transition_start_seconds);
    const float progress = context->theme_transition_duration_seconds <= 0.0
        ? 1.0f
        : static_cast<float>(std::min(1.0, elapsed / context->theme_transition_duration_seconds));
    float eased = context->theme_transition_curve.Evaluate(progress);
    if (!std::isfinite(eased)) eased = progress;
    eased = Clamp(eased, 0.0f, 1.0f);
    context->theme = BlendTheme(context->theme_from, context->theme_target, eased);
    if (progress >= 1.0f) {
        context->theme = context->theme_target;
        context->theme_from = context->theme_target;
        context->theme_transition_active = false;
    } else if (context->motion_time_scale > 0.0f && !context->reduce_motion) {
        context->needs_frame = true;
    }
}

void StartThemeTransitionInternal(Context* context, const Theme& target) noexcept {
    if (context == nullptr) return;
    context->theme_target = target;
    const double duration = target.color_transition.duration_seconds;
    context->theme_transition_duration_seconds = std::isfinite(duration) ? std::max(0.0, duration) : 0.0;
    context->theme_transition_curve = target.color_transition.curve;
    context->theme_from = context->theme;
    context->theme_transition_start_seconds = context->motion_time_seconds;
    // Before the first frame there is no established motion-clock baseline; apply initial appearance immediately.
    context->theme_transition_active = context->clock_initialized && !context->reduce_motion &&
                                       context->theme_transition_duration_seconds > 0.0;
    if (!context->theme_transition_active) {
        context->theme = target;
        context->theme_from = target;
    } else {
        // Preserve the currently rendered values when another theme change interrupts this one.
        AdvanceThemeTransition(context);
    }
    RequestRedraw(context);
}

} // namespace

namespace detail {
void BeginThemeTransition(Context* context, const Theme& theme) noexcept {
    StartThemeTransitionInternal(context, theme);
}
} // namespace detail

Context* CreateContext(const Config& config) noexcept {
    if (!IsValidConfig(config)) {
        return nullptr;
    }

    auto* imgui_context = detail::BridgeCurrentImGuiContext();
    if (imgui_context == nullptr) {
        return nullptr;
    }

    void* memory = std::malloc(sizeof(Context));
    if (memory == nullptr) {
        return nullptr;
    }
    auto* context = new (memory) Context{};
    context->config = config;
    context->imgui_context = imgui_context;
    context->clock = config.clock != nullptr ? config.clock : &context->default_clock;
    context->theme = detail::DefaultTheme(ThemeKind::Dark);
    context->theme_from = context->theme;
    context->theme_target = context->theme;

    if (!context->frame_arena.Initialize(config.frame_arena_bytes) ||
        !context->state_store.Initialize(config.state_capacity, config.max_state_bytes) ||
        !context->motion_scheduler.Initialize(config.motion_capacity)) {
        context->~Context();
        std::free(context);
        return nullptr;
    }
    context->needs_frame = true;
    return context;
}

void DestroyContext(Context* context) noexcept {
    if (context == nullptr) return;
    if (g_current_context == context) {
        g_current_context = nullptr;
    }
    context->~Context();
    std::free(context);
}

bool BeginFrame(Context* context, const Viewport& viewport, float delta_seconds) noexcept {
    if (context == nullptr || context->in_frame || context->imgui_context == nullptr ||
        (g_current_context != nullptr && g_current_context != context)) {
        return false;
    }
    if (detail::BridgeCurrentImGuiContext() != context->imgui_context) {
        return false;
    }
    Viewport normalized_viewport{};
    if (!NormalizeViewport(viewport, &normalized_viewport)) return false;

    context->frame_arena.Reset();
    ++context->frame_info.index;
    context->frame_info.delta_seconds = std::isfinite(delta_seconds) ? Clamp(delta_seconds, 0.0f, 0.1f) : 0.0f;
    double clock_now = context->clock->NowSeconds();
    if (!std::isfinite(clock_now)) {
        // A broken/custom host clock must not let NaN/Inf enter frame or motion state.
        clock_now = context->clock_initialized ? context->last_clock_time_seconds : 0.0;
    }
    if (!context->clock_initialized) {
        context->motion_time_seconds = clock_now;
        context->last_clock_time_seconds = clock_now;
        context->clock_initialized = true;
    } else {
        // Preserve the high-water mark. Merely discarding a negative delta and
        // storing the rewound timestamp would double-count time when it recovers.
        clock_now = std::max(clock_now, context->last_clock_time_seconds);
        double elapsed = clock_now - context->last_clock_time_seconds;
        if (!std::isfinite(elapsed) || elapsed < 0.0) elapsed = 0.0;
        const double scaled_elapsed = elapsed * static_cast<double>(context->motion_time_scale);
        const double next_motion_time = context->motion_time_seconds + scaled_elapsed;
        if (std::isfinite(next_motion_time)) context->motion_time_seconds = next_motion_time;
        context->last_clock_time_seconds = clock_now;
    }
    context->frame_info.time_seconds = context->motion_time_seconds;
    // Portal timeouts are routed through Presence so timed toasts and transient layers
    // receive the same exit transition as explicit CloseLayer() calls. LayerStack itself
    // remains a deterministic metadata container; the context owns the visual lifecycle.
    for (auto& runtime : context->portal_runtime) {
        if (!runtime.used) continue;
        if (context->layers.Find(runtime.id) == nullptr) {
            runtime = {};
            continue;
        }
        if (runtime.target_present && runtime.expires_at > 0.0 &&
            context->motion_time_seconds >= runtime.expires_at) {
            runtime.target_present = false;
            runtime.expires_at = 0.0;
            const double duration = context->reduce_motion ? 0.0 : context->theme.color_transition.duration_seconds;
            runtime.presence.SetPresent(false, context->motion_time_seconds, duration,
                                        context->theme.color_transition.curve);
            context->needs_frame = true;
        }
        if (runtime.presence.Advance(context->motion_time_seconds)) {
            context->needs_frame = true;
        } else if (!runtime.target_present && !runtime.presence.IsMounted()) {
            context->layers.Close(runtime.id);
            runtime = {};
            context->needs_frame = true;
        }
    }
    context->frame_info.root_id = detail::BridgeCurrentRootId(context->imgui_context);
    context->frame_info.viewport = normalized_viewport;
    context->state_store.BeginFrame(context->frame_info.index);
    context->state_store.Sweep(context->config.state_ttl_frames, context->stats.state_evictions);
    context->in_frame = true;
    context->needs_frame = false;
    context->implicit_animations_active = false;
    g_current_context = context;
    AdvanceThemeTransition(context);
    detail::ResetNodeFrame(context);
    if (context->motion_scheduler.Advance(context->frame_info.time_seconds, context->reduce_motion) &&
        context->motion_time_scale > 0.0f) {
        context->needs_frame = true;
    }

    ++context->stats.frames_started;
    context->stats.in_frame = true;
    return true;
}

bool EndFrame(Context* context) noexcept {
    if (context == nullptr || !context->in_frame) {
        return false;
    }

    const bool nodes_ok = detail::FinishNodeFrame(context);
    context->stats.state_entries = context->state_store.EntryCount();
    context->stats.nodes_built = context->nodes_built;
    context->stats.nodes_painted = context->nodes_painted;
    context->stats.duplicate_node_ids = context->duplicate_node_ids;
    context->stats.arena_bytes_used = context->frame_arena.Used();
    context->stats.arena_high_water_bytes = context->frame_arena.HighWater();
    context->stats.arena_failed_allocations = context->frame_arena.FailedAllocations();
    context->in_frame = false;
    context->stats.in_frame = false;
    if (g_current_context == context) {
        g_current_context = nullptr;
    }
    return nodes_ok;
}

bool NeedsFrame(const Context* context) noexcept {
    if (context == nullptr) return false;
    if (context->needs_frame) return true;
    for (const auto& runtime : context->portal_runtime) {
        if (!runtime.used) continue;
        if (runtime.presence.NeedsFrame() || (runtime.target_present && runtime.expires_at > 0.0)) return true;
    }
    return false;
}

void RequestRedraw(Context* context) noexcept {
    if (context != nullptr) {
        context->needs_frame = true;
    }
}

bool AttachController(Context* context, Controller* controller) noexcept {
    if (context == nullptr || controller == nullptr ||
        (controller->owner_context_ != nullptr && controller->owner_context_ != context) ||
        !context->motion_scheduler.Attach(controller)) return false;
    controller->owner_context_ = context;
    if (controller->NeedsFrame()) RequestRedraw(context);
    return true;
}

bool DetachController(Context* context, Controller* controller) noexcept {
    if (context == nullptr || controller == nullptr || !context->motion_scheduler.Detach(controller)) return false;
    if (controller->owner_context_ == context) controller->owner_context_ = nullptr;
    return true;
}

void SetMotionTimeScale(Context* context, float scale) noexcept {
    if (context == nullptr) return;
    context->motion_time_scale = std::isfinite(scale) ? Clamp(scale, 0.0f, 10.0f) : 1.0f;
    if (context->motion_time_scale > 0.0f &&
        (context->motion_scheduler.ActiveCount() > 0 || context->implicit_animations_active || context->theme_transition_active)) {
        RequestRedraw(context);
    }
}

float GetMotionTimeScale(const Context* context) noexcept {
    return context != nullptr ? context->motion_time_scale : 1.0f;
}

void SetReduceMotion(Context* context, bool enabled) noexcept {
    if (context == nullptr) return;
    context->reduce_motion = enabled;
    bool snapped_theme = false;
    bool snapped_portal = false;
    if (enabled) {
        for (auto& runtime : context->portal_runtime) {
            if (!runtime.used || !runtime.presence.IsAnimating()) continue;
            runtime.presence.SetPresent(runtime.target_present, context->motion_time_seconds, 0.0);
            snapped_portal = true;
        }
    }
    if (enabled && context->theme_transition_active) {
        context->theme = context->theme_target;
        context->theme_from = context->theme_target;
        context->theme_transition_active = false;
        snapped_theme = true;
    }
    if (snapped_theme || snapped_portal || (context->motion_time_scale > 0.0f &&
        (context->motion_scheduler.ActiveCount() > 0 || context->implicit_animations_active || context->theme_transition_active))) {
        RequestRedraw(context);
    }
}

bool IsReduceMotionEnabled(const Context* context) noexcept {
    return context != nullptr && context->reduce_motion;
}

const FrameInfo* GetFrameInfo(const Context* context) noexcept {
    return context != nullptr ? &context->frame_info : nullptr;
}

DebugStats GetDebugStats(const Context* context) noexcept {
    if (context == nullptr) return {};
    DebugStats stats = context->stats;
    stats.state_entries = context->state_store.EntryCount();
    stats.nodes_built = context->nodes_built;
    stats.nodes_painted = context->nodes_painted;
    stats.duplicate_node_ids = context->duplicate_node_ids;
    stats.arena_bytes_used = context->frame_arena.Used();
    stats.arena_high_water_bytes = context->frame_arena.HighWater();
    stats.arena_failed_allocations = context->frame_arena.FailedAllocations();
    stats.in_frame = context->in_frame;
    return stats;
}

void* FrameAlloc(Context* context, std::size_t size, std::size_t alignment) noexcept {
    if (context == nullptr || !context->in_frame) {
        return nullptr;
    }
    void* result = context->frame_arena.Allocate(size, alignment);
    if (result == nullptr) {
        context->stats.arena_failed_allocations = context->frame_arena.FailedAllocations();
    }
    return result;
}

void PinState(Context* context, std::uint64_t id, bool pinned) noexcept {
    if (context != nullptr) {
        context->state_store.Pin(id, pinned);
    }
}

namespace detail {

void* GetState(Context* context, std::uint64_t id, std::size_t size, std::size_t alignment,
               const void* initial_value) noexcept {
    if (context == nullptr || !context->in_frame) {
        return nullptr;
    }
    return context->state_store.Get(id, size, alignment, initial_value,
        context->stats.state_hits, context->stats.state_misses,
        context->stats.state_capacity_exhaustions);
}

void MarkImplicitMotionActive(Context* context) noexcept {
    if (context == nullptr) return;
    context->implicit_animations_active = true;
    if (context->motion_time_scale > 0.0f) RequestRedraw(context);
}

} // namespace detail

void InjectMousePosition(Context* context, Vec2 position) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectMousePosition(context->imgui_context, position);
    RequestRedraw(context);
}

void InjectMouseButton(Context* context, int button, bool down) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectMouseButton(context->imgui_context, button, down);
    RequestRedraw(context);
}

void InjectMouseWheel(Context* context, Vec2 wheel) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectMouseWheel(context->imgui_context, wheel);
    RequestRedraw(context);
}

void InjectKey(Context* context, ImGuiKey key, bool down) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectKey(context->imgui_context, key, down);
    RequestRedraw(context);
}

void InjectText(Context* context, const char* utf8_text) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectText(context->imgui_context, utf8_text);
    RequestRedraw(context);
}

const char* GetClipboardText(Context* context) noexcept {
    return context != nullptr ? detail::BridgeGetClipboardText(context->imgui_context) : nullptr;
}

void SetClipboardText(Context* context, const char* utf8_text) noexcept {
    if (context != nullptr) detail::BridgeSetClipboardText(context->imgui_context, utf8_text);
}

void InjectFocus(Context* context, bool focused) noexcept {
    if (context == nullptr) return;
    detail::BridgeInjectFocus(context->imgui_context, focused);
    RequestRedraw(context);
}

} // namespace Lumen
