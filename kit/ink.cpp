#include "lumen/kit/ink.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kInkStartSize = 4.0f;

struct InkState {
    Vec2 origin{};
    double start_seconds = 0.0;
    bool active = false;
};

// Frame-arena record shared by the node's event handler and painter.
struct InkSurface {
    EventHandler handler = nullptr;
    void* handler_data = nullptr;
    CustomPaint paint{};
    CustomPaint ink_paint{};
    Color tint{};
    float radius = 0.0f;
    bool has_tint = false;
    bool disabled = false;
};

std::uint64_t InkStateId(std::uint64_t node_id) noexcept { return MakeId(node_id, "lumen-kit-ink"); }

double Now(const Context* context) noexcept {
    const FrameInfo* frame = GetFrameInfo(context);
    return frame != nullptr ? frame->time_seconds : 0.0;
}

void HandleInkEvent(Context* context, UIEvent& event, void* user_data) noexcept {
    const auto* surface = static_cast<const InkSurface*>(user_data);
    if (surface->handler != nullptr) surface->handler(context, event, surface->handler_data);
    if (event.type != UIEventType::PointerDown || event.phase == UIEventPhase::Capture ||
        event.button != ImGuiMouseButton_Left || event.default_prevented || surface->disabled ||
        IsReduceMotionEnabled(context)) return;
    // Target dispatch reaches the innermost surface first; ancestors bubbling the same press skip it.
    const FrameInfo* frame = GetFrameInfo(context);
    std::uint64_t* claimed = State<std::uint64_t>(context, MakeId(0u, "lumen-kit-ink-press"));
    if (frame == nullptr || claimed == nullptr || *claimed == frame->index + 1u) return;
    InkState* state = State<InkState>(context, InkStateId(event.current_target_id));
    if (state == nullptr) return;
    *claimed = frame->index + 1u;
    *state = InkState{event.position, Now(context), true};
    RequestRedraw(context);
}

void PaintInk(Context* context, Canvas& canvas, void* user_data) noexcept {
    const auto* surface = static_cast<const InkSurface*>(user_data);
    if (surface->paint.painter != nullptr) surface->paint.painter(context, canvas, surface->paint.user_data);
    InkState* state = State<InkState>(context, InkStateId(canvas.NodeId()));
    if (state == nullptr || !state->active) return;
    if (IsReduceMotionEnabled(context)) {
        state->active = false;
        return;
    }

    const Theme theme = GetTheme(context);
    const TweenSpec& spec = theme.ink_transition;
    const double elapsed = Now(context) - state->start_seconds - std::max(0.0, spec.delay_seconds);
    const double duration = std::isfinite(spec.duration_seconds) ? std::max(0.0, spec.duration_seconds) : 0.0;
    const float linear = duration > 0.0 ? static_cast<float>(std::min(1.0, std::max(0.0, elapsed / duration))) : 1.0f;
    if (linear >= 1.0f) {
        state->active = false;
        return;
    }
    const float eased = spec.curve.Evaluate(linear);
    const float t = Clamp(std::isfinite(eased) ? eased : linear, 0.0f, 1.0f);

    const Rect bounds = canvas.Bounds();
    const float half = 0.5f * std::min(kInkStartSize, std::min(bounds.Width(), bounds.Height()));
    const Vec2 origin{Clamp(state->origin.x, bounds.min.x + half, bounds.max.x - half),
                      Clamp(state->origin.y, bounds.min.y + half, bounds.max.y - half)};
    const Rect ink{{Lerp(origin.x - half, bounds.min.x, t), Lerp(origin.y - half, bounds.min.y, t)},
                   {Lerp(origin.x + half, bounds.max.x, t), Lerp(origin.y + half, bounds.max.y, t)}};
    // Round like a circle while small, settling onto the node's corner radius as it fills.
    const float round = 0.5f * std::min(ink.Width(), ink.Height());
    const float corner = std::min(surface->radius, 0.5f * std::min(bounds.Width(), bounds.Height()));
    Color color = surface->has_tint ? surface->tint : theme.ink;
    color.a = theme.ink.a * (1.0f - t);
    canvas.FillRect(ink, color, Lerp(round, corner, t));
    RequestRedraw(context);
}

} // namespace

NodeStyle WithInk(Context* context, const NodeStyle& style, const Color* tint) noexcept {
    void* memory = FrameAlloc(context, sizeof(InkSurface), alignof(InkSurface));
    if (memory == nullptr) return style;
    auto* surface = new (memory) InkSurface{};
    surface->handler = style.event_handler;
    surface->handler_data = style.event_user_data;
    if (style.custom_paint != nullptr) surface->paint = *style.custom_paint;
    surface->ink_paint = CustomPaint{PaintInk, surface};
    if (tint != nullptr) {
        surface->tint = *tint;
        surface->has_tint = true;
    }
    surface->radius = std::max(0.0f, style.radius >= 0.0f ? style.radius : GetTheme(context).radius);
    surface->disabled = style.disabled;

    NodeStyle ink_style = style;
    ink_style.event_handler = HandleInkEvent;
    ink_style.event_user_data = surface;
    ink_style.custom_paint = &surface->ink_paint;
    return ink_style;
}

bool BeginInkSurface(Context* context, const char* key, const NodeStyle& style) noexcept {
    return BeginNode(context, key, WithInk(context, style));
}

} // namespace Lumen::Kit
