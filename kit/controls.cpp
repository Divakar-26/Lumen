#include "lumen/kit/controls.h"
#include "lumen/kit/button.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kTrackWidth = 52.0f;
constexpr float kTrackHeight = 32.0f;
constexpr float kTrackBorder = 2.0f;
constexpr float kLabelGap = 14.0f;
constexpr float kThumbOffRadius = 8.0f;
constexpr float kThumbOnRadius = 12.0f;
constexpr float kThumbPressedGrowth = 2.0f;
constexpr float kHaloRadius = 20.0f;
constexpr float kHaloAlpha = 0.08f;
constexpr float kDisabledAlpha = 0.38f;
constexpr float kSegmentHeight = 36.0f;

struct SwitchData {
    bool* value = nullptr;
    bool on = false;
    bool disabled = false;
};

struct SegmentData {
    int* selected = nullptr;
    int index = 0;
};

Color Mix(Color from, Color to, float t) noexcept {
    return Color{Lerp(from.r, to.r, t), Lerp(from.g, to.g, t), Lerp(from.b, to.b, t), Lerp(from.a, to.a, t)};
}

Color WithAlpha(Color color, float alpha) noexcept {
    color.a = alpha;
    return color;
}

void HandleSwitchEvent(Context*, UIEvent& event, void* user_data) noexcept {
    auto* data = static_cast<SwitchData*>(user_data);
    if (event.type != UIEventType::Click || event.phase != UIEventPhase::Target || event.default_prevented ||
        data->disabled || data->value == nullptr) return;
    *data->value = !*data->value;
}

void PaintSwitch(Context* context, Canvas& canvas, void* user_data) noexcept {
    const auto* data = static_cast<const SwitchData*>(user_data);
    const Theme theme = GetTheme(context);
    const NodeInteractionState state = GetNodeState(context, canvas.NodeId());
    const float progress = Animate(context, MakeId(canvas.NodeId(), "kit-switch"), data->on ? 1.0f : 0.0f,
                                   theme.toggle_transition);
    const float t = Clamp(progress, 0.0f, 1.0f);

    const Rect bounds = canvas.Bounds();
    const float top = bounds.min.y + 0.5f * (bounds.Height() - kTrackHeight);
    const Rect track{bounds.min.x, top, kTrackWidth, kTrackHeight};
    const float alpha = data->disabled ? kDisabledAlpha : 1.0f;
    const Color fill = WithAlpha(Mix(theme.surface_alt, theme.accent, t), alpha);
    canvas.FillRect(track, fill, 0.5f * kTrackHeight);
    if (t < 1.0f) {
        const float inset = 0.5f * kTrackBorder;
        const Rect border{{track.min.x + inset, track.min.y + inset}, {track.max.x - inset, track.max.y - inset}};
        canvas.StrokeRect(border, WithAlpha(theme.border, alpha * (1.0f - t)), 0.5f * kTrackHeight - inset, kTrackBorder);
    }

    // The thumb position uses the unclamped progress so the curve's overshoot is visible.
    const float off_x = track.min.x + 0.5f * kTrackHeight;
    const float on_x = track.max.x - 0.5f * kTrackHeight;
    const Vec2 center{Lerp(off_x, on_x, progress), track.min.y + 0.5f * kTrackHeight};
    if (!data->disabled && (state.hovered || state.pressed)) {
        canvas.FillCircle(center, kHaloRadius, WithAlpha(data->on ? theme.accent : theme.text, kHaloAlpha));
    }
    float radius = Lerp(kThumbOffRadius, kThumbOnRadius, t);
    if (state.pressed && !data->disabled) radius += kThumbPressedGrowth;
    canvas.FillCircle(center, radius, WithAlpha(Mix(theme.border, theme.accent_text, t), alpha));
}

void SelectSegment(Context*, std::uint64_t, void* user_data) noexcept {
    const auto* data = static_cast<const SegmentData*>(user_data);
    if (data->selected != nullptr) *data->selected = data->index;
}

} // namespace

NodeStyle CardStyle(const Theme& theme) noexcept {
    NodeStyle style{};
    style.layout = LayoutMode::Column;
    style.padding = {16.0f, 18.0f, 16.0f, 18.0f};
    style.gap = 8.0f;
    style.radius = theme.radius;
    style.background = theme.surface;
    style.has_background = true;
    return style;
}

bool Switch(Context* context, const char* key, const char* label, bool* value, const NodeStyle& style) noexcept {
    void* memory = FrameAlloc(context, sizeof(SwitchData), alignof(SwitchData));
    if (memory == nullptr) return false;
    auto* data = new (memory) SwitchData{value, value != nullptr && *value, style.disabled || value == nullptr};
    const CustomPaint paint{PaintSwitch, data};
    const Theme theme = GetTheme(context);

    NodeStyle row = style;
    row.layout = LayoutMode::Row;
    row.align_items = Align::Center;
    row.min_height = std::max(row.min_height, kTrackHeight);
    row.padding.left = std::max(row.padding.left, kTrackWidth + kLabelGap);
    row.radius = row.radius >= 0.0f ? row.radius : 0.5f * kTrackHeight;
    row.focusable = !data->disabled;
    row.disabled = data->disabled;
    row.event_handler = HandleSwitchEvent;
    row.event_user_data = data;
    row.custom_paint = &paint;
    if (!BeginNode(context, key, row)) return false;
    NodeStyle text{};
    text.font_size = theme.font_size;
    text.pointer_events = false;
    if (data->disabled) {
        text.text_color = theme.muted_text;
        text.has_text_color = true;
    }
    Label(context, "label", label != nullptr ? label : "", text);
    EndNode(context);
    return true;
}

bool Segmented(Context* context, const char* key, const char* const* labels, int count, int* selected,
               float item_width) noexcept {
    if (labels == nullptr || count <= 0) return false;
    NodeStyle row{};
    row.layout = LayoutMode::Row;
    row.gap = 8.0f;
    row.align_items = Align::Center;
    if (!BeginNode(context, key, row)) return false;
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        void* memory = FrameAlloc(context, sizeof(SegmentData), alignof(SegmentData));
        if (memory == nullptr) {
            ok = false;
            break;
        }
        auto* data = new (memory) SegmentData{selected, i};
        const bool active = selected != nullptr && *selected == i;
        const ButtonVariant variant = active ? ButtonVariant::Filled : ButtonVariant::Outlined;
        NodeStyle style = ButtonStyle(context, variant);
        style.shadow = nullptr;
        style.height = kSegmentHeight;
        style.radius = 0.5f * kSegmentHeight;
        style.padding.left = style.padding.right = 12.0f;
        if (item_width > 0.0f) style.width = item_width;
        char item_key[16];
        std::snprintf(item_key, sizeof(item_key), "item-%d", i);
        ok = Button(context, item_key, labels[i] != nullptr ? labels[i] : "", variant, SelectSegment, data, style) && ok;
    }
    EndNode(context);
    return ok;
}

} // namespace Lumen::Kit
