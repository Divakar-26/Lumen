#include "lumen/kit/overlays.h"
#include "lumen/kit/button.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kDialogWidth = 312.0f;
constexpr float kDialogPadding = 24.0f;
constexpr float kDialogRadius = 28.0f;
constexpr float kDialogRise = 28.0f;

constexpr float kToastWidth = 344.0f;
constexpr float kToastPadding = 14.0f;
constexpr float kToastRadius = 16.0f;
constexpr float kToastGap = 10.0f;
constexpr float kToastEdgeMargin = 24.0f;
constexpr float kBadgeSize = 28.0f;
constexpr float kBadgeMaxScale = 1.18f;
constexpr float kColumnGap = 12.0f;
constexpr float kTextGap = 3.0f;
constexpr float kBarHeight = 3.0f;
constexpr float kBarGap = 12.0f;
constexpr float kCloseSize = 28.0f;
constexpr float kActionHeight = 28.0f;
constexpr float kActionPadding = 8.0f;
constexpr float kEnterRise = 36.0f;
constexpr float kEnterSlide = 70.0f;
constexpr float kExitDrop = 22.0f;
constexpr float kExitSlide = 90.0f;
constexpr float kEnterFadePortion = 0.23f;

const ShadowStyle kDialogShadow = [] {
    ShadowStyle shadow{};
    shadow.blur = 40.0f;
    shadow.offset_y = 10.0f;
    return shadow;
}();

const ShadowStyle kToastShadow = [] {
    ShadowStyle shadow{};
    shadow.blur = 16.0f;
    shadow.offset_y = 6.0f;
    shadow.color = Color{0.0f, 0.0f, 0.0f, 0.30f};
    shadow.has_color = true;
    return shadow;
}();

Color WithAlpha(Color color, float alpha) noexcept {
    color.a = alpha;
    return color;
}

NodeStyle TextStyle(float size, Color color) noexcept {
    NodeStyle style{};
    style.font_size = size;
    style.text_color = color;
    style.has_text_color = true;
    style.pointer_events = false;
    return style;
}

// ---- Alert dialog --------------------------------------------------------------------------

struct DialogState {
    DialogResult pending = DialogResult::None;
    bool reported = false;
};

struct DialogChoice {
    std::uint64_t layer_id = 0u;
    DialogResult result = DialogResult::None;
};

std::uint64_t DialogStateId(std::uint64_t layer_id) noexcept { return MakeId(layer_id, "kit-dialog"); }

void ChooseDialog(Context* context, std::uint64_t, void* user_data) noexcept {
    const auto* choice = static_cast<const DialogChoice*>(user_data);
    if (DialogState* state = State<DialogState>(context, DialogStateId(choice->layer_id))) {
        if (state->pending == DialogResult::None) state->pending = choice->result;
    }
    CloseLayer(context, choice->layer_id);
}

bool DialogButton(Context* context, const char* key, const char* label, std::uint64_t layer_id, DialogResult result) noexcept {
    void* memory = FrameAlloc(context, sizeof(DialogChoice), alignof(DialogChoice));
    if (memory == nullptr) return false;
    auto* choice = new (memory) DialogChoice{layer_id, result};
    return Button(context, key, label, ButtonVariant::Text, ChooseDialog, choice);
}

// ---- Toasts ------------------------------------------------------------------------------

struct ToastTarget {
    ToastQueue* queue = nullptr;
    std::uint64_t id = 0u;
};

struct ToastPaint {
    ToastTarget target{};
    float progress = 1.0f;
    float badge_scale = 1.0f;
    Color accent{};
    Color track{};
    Color glyph{};
    ToastStatus status = ToastStatus::Info;
};

ToastQueue::Entry* FindToast(ToastQueue* queue, std::uint64_t id) noexcept {
    if (queue == nullptr) return nullptr;
    for (int i = 0; i < queue->count; ++i) {
        if (queue->entries[i].id == id) return &queue->entries[i];
    }
    return nullptr;
}

void CopyText(char* destination, std::size_t capacity, const char* source) noexcept {
    destination[0] = '\0';
    if (source == nullptr) return;
    std::strncpy(destination, source, capacity - 1u);
    destination[capacity - 1u] = '\0';
}

float Progress(double now, double start, const TweenSpec& spec, bool reduce_motion, float* linear_out = nullptr) noexcept {
    const double duration = std::isfinite(spec.duration_seconds) ? std::max(0.0, spec.duration_seconds) : 0.0;
    const float linear = (reduce_motion || duration <= 0.0) ? 1.0f
        : static_cast<float>(std::min(1.0, std::max(0.0, (now - start) / duration)));
    if (linear_out != nullptr) *linear_out = linear;
    const float eased = spec.curve.Evaluate(linear);
    return std::isfinite(eased) ? eased : linear;
}

float TextHeight(const char* text, float size, float wrap_width) noexcept {
    if (text == nullptr || text[0] == '\0') return 0.0f;
    ImFont* font = ImGui::GetFont();
    if (font == nullptr) return size;
    return font->CalcTextSizeA(size, FLT_MAX, std::max(1.0f, wrap_width), text).y;
}

float ActionWidth(const char* action, float size) noexcept {
    if (action == nullptr || action[0] == '\0') return 0.0f;
    ImFont* font = ImGui::GetFont();
    const float text = font != nullptr ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, action).x : size * 4.0f;
    return text + 2.0f * kActionPadding;
}

float TextColumnWidth(const ToastQueue::Entry& entry, const Theme& theme) noexcept {
    const float action = ActionWidth(entry.action, theme.caption_size);
    return kToastWidth - 2.0f * kToastPadding - kBadgeSize - kColumnGap - kCloseSize - kColumnGap -
           (action > 0.0f ? action + kColumnGap : 0.0f);
}

float ToastHeight(const ToastQueue::Entry& entry, const Theme& theme) noexcept {
    const float wrap = TextColumnWidth(entry, theme);
    const float text = TextHeight(entry.title, theme.font_size, wrap) +
                       (entry.message[0] != '\0' ? kTextGap + TextHeight(entry.message, theme.caption_size, wrap) : 0.0f);
    return kToastPadding + std::max(kBadgeSize, text) + kBarGap + kBarHeight + kToastPadding;
}

void CloseToast(Context*, std::uint64_t, void* user_data) noexcept {
    const auto* target = static_cast<const ToastTarget*>(user_data);
    if (ToastQueue::Entry* entry = FindToast(target->queue, target->id)) entry->close_requested = true;
}

void ToastAction(Context*, std::uint64_t, void* user_data) noexcept {
    const auto* target = static_cast<const ToastTarget*>(user_data);
    if (ToastQueue::Entry* entry = FindToast(target->queue, target->id)) entry->action_requested = true;
}

void PaintToastCard(Context* context, Canvas& canvas, void* user_data) noexcept {
    const auto* paint = static_cast<const ToastPaint*>(user_data);
    if (ToastQueue::Entry* entry = FindToast(paint->target.queue, paint->target.id)) {
        entry->hovered = GetNodeState(context, canvas.NodeId()).hovered;
    }
    const Rect bounds = canvas.Bounds();
    const Rect track{{bounds.min.x + kToastPadding, bounds.max.y - kToastPadding - kBarHeight},
                     {bounds.max.x - kToastPadding, bounds.max.y - kToastPadding}};
    canvas.FillRect(track, paint->track, 0.5f * kBarHeight);
    const Rect fill{track.min, {Lerp(track.min.x, track.max.x, Clamp(paint->progress, 0.0f, 1.0f)), track.max.y}};
    canvas.FillRect(fill, paint->accent, 0.5f * kBarHeight);
}

void PaintBadge(Context*, Canvas& canvas, void* user_data) noexcept {
    const auto* paint = static_cast<const ToastPaint*>(user_data);
    const Rect bounds = canvas.Bounds();
    const Vec2 c{0.5f * (bounds.min.x + bounds.max.x), 0.5f * (bounds.min.y + bounds.max.y)};
    const float s = paint->badge_scale;
    canvas.FillCircle(c, 0.5f * kBadgeSize * s, paint->accent);
    const float k = s;
    const float stroke = 2.2f * k;
    switch (paint->status) {
        case ToastStatus::Success:
            canvas.Line({c.x - 5.5f * k, c.y + 0.5f * k}, {c.x - 1.5f * k, c.y + 4.5f * k}, paint->glyph, stroke);
            canvas.Line({c.x - 1.5f * k, c.y + 4.5f * k}, {c.x + 6.0f * k, c.y - 4.5f * k}, paint->glyph, stroke);
            break;
        case ToastStatus::Error:
            canvas.Line({c.x - 4.5f * k, c.y - 4.5f * k}, {c.x + 4.5f * k, c.y + 4.5f * k}, paint->glyph, stroke);
            canvas.Line({c.x + 4.5f * k, c.y - 4.5f * k}, {c.x - 4.5f * k, c.y + 4.5f * k}, paint->glyph, stroke);
            break;
        case ToastStatus::Info:
            canvas.FillCircle({c.x, c.y - 5.0f * k}, 1.6f * k, paint->glyph);
            canvas.Line({c.x, c.y - 1.5f * k}, {c.x, c.y + 6.0f * k}, paint->glyph, stroke);
            break;
        case ToastStatus::Warning:
            canvas.Line({c.x, c.y - 6.0f * k}, {c.x, c.y + 1.5f * k}, paint->glyph, stroke);
            canvas.FillCircle({c.x, c.y + 5.0f * k}, 1.6f * k, paint->glyph);
            break;
    }
}

void PaintClose(Context* context, Canvas& canvas, void*) noexcept {
    const Theme theme = GetTheme(context);
    const bool hovered = GetNodeState(context, canvas.NodeId()).hovered;
    const Rect b = canvas.Bounds();
    const Vec2 c{0.5f * (b.min.x + b.max.x), 0.5f * (b.min.y + b.max.y)};
    const Color color = WithAlpha(theme.inverse_on_surface, hovered ? 1.0f : 0.7f);
    canvas.Line({c.x - 5.0f, c.y - 5.0f}, {c.x + 5.0f, c.y + 5.0f}, color, 1.8f);
    canvas.Line({c.x + 5.0f, c.y - 5.0f}, {c.x - 5.0f, c.y + 5.0f}, color, 1.8f);
}

Color StatusAccent(const Theme& theme, ToastStatus status) noexcept {
    switch (status) {
        case ToastStatus::Success: return theme.inverse_success;
        case ToastStatus::Warning: return theme.inverse_warning;
        case ToastStatus::Error: return theme.inverse_danger;
        case ToastStatus::Info: break;
    }
    return theme.inverse_info;
}

void DeclareToast(Context* context, ToastQueue& queue, ToastQueue::Entry& entry, const Theme& theme,
                  Vec2 position, float height, float opacity, float badge_scale) noexcept {
    void* target_memory = FrameAlloc(context, sizeof(ToastTarget), alignof(ToastTarget));
    void* paint_memory = FrameAlloc(context, sizeof(ToastPaint), alignof(ToastPaint));
    if (target_memory == nullptr || paint_memory == nullptr) return;
    auto* target = new (target_memory) ToastTarget{&queue, entry.id};
    auto* paint = new (paint_memory) ToastPaint{};
    paint->target = *target;
    paint->progress = entry.duration > 0.0 ? static_cast<float>(entry.remaining / entry.duration) : 0.0f;
    paint->badge_scale = badge_scale;
    paint->accent = StatusAccent(theme, entry.status);
    paint->track = WithAlpha(theme.inverse_on_surface, 0.12f);
    paint->glyph = theme.inverse_surface;
    paint->status = entry.status;

    const CustomPaint card_paint{PaintToastCard, paint};
    NodeStyle card{};
    card.layout = LayoutMode::Row;
    card.align_items = Align::Start;
    card.gap = kColumnGap;
    card.x = position.x;
    card.y = position.y;
    card.width = kToastWidth;
    card.height = height;
    card.opacity = opacity;
    card.padding = {kToastPadding, kToastPadding, kToastPadding + kBarGap + kBarHeight, kToastPadding};
    card.radius = kToastRadius;
    card.background = theme.inverse_surface;
    card.has_background = true;
    // Same fill on hover: it only makes the card a hit target so hovering pauses the countdown.
    card.hover_background = theme.inverse_surface;
    card.has_hover_background = true;
    card.shadow = &kToastShadow;
    card.custom_paint = &card_paint;
    card.animate_layout = false;
    if (!BeginNode(context, entry.id, card)) return;

    const CustomPaint badge_paint{PaintBadge, paint};
    NodeStyle badge{};
    badge.width = kBadgeSize;
    badge.height = kBadgeSize;
    badge.custom_paint = &badge_paint;
    badge.pointer_events = false;
    BeginNode(context, "badge", badge);
    EndNode(context);

    NodeStyle column{};
    column.layout = LayoutMode::Column;
    column.gap = kTextGap;
    column.flex_grow = 1.0f;
    column.flex_basis = 0.0f;
    column.pointer_events = false;
    BeginNode(context, "text", column);
    Label(context, "title", entry.title, TextStyle(theme.font_size, theme.inverse_on_surface));
    if (entry.message[0] != '\0') Label(context, "message", entry.message, TextStyle(theme.caption_size, theme.outline_variant));
    EndNode(context);

    if (entry.action[0] != '\0') {
        NodeStyle action = ButtonStyle(context, ButtonVariant::Text);
        action.height = kActionHeight;
        action.radius = 0.5f * kActionHeight;
        action.padding = {0.0f, kActionPadding, 0.0f, kActionPadding};
        action.font_size = theme.caption_size;
        action.text_color = theme.inverse_primary;
        action.background = WithAlpha(theme.inverse_primary, 0.0f);
        action.hover_background = WithAlpha(theme.inverse_primary, 0.12f);
        action.pressed_background = WithAlpha(theme.inverse_primary, 0.16f);
        Button(context, "action", entry.action, ButtonVariant::Text, ToastAction, target, action);
    }

    const CustomPaint close_paint{PaintClose, nullptr};
    NodeStyle close{};
    close.width = kCloseSize;
    close.height = kCloseSize;
    close.min_height = kCloseSize;
    close.padding = {0.0f, 0.0f, 0.0f, 0.0f};
    close.radius = 0.5f * kCloseSize;
    close.background = WithAlpha(theme.inverse_on_surface, 0.0f);
    close.has_background = true;
    close.hover_background = WithAlpha(theme.inverse_on_surface, 0.10f);
    close.has_hover_background = true;
    close.pressed_background = WithAlpha(theme.inverse_on_surface, 0.16f);
    close.has_pressed_background = true;
    close.custom_paint = &close_paint;
    Lumen::Button(context, "close", "", CloseToast, target, close);
    EndNode(context);
}

} // namespace

bool OpenAlertDialog(Context* context, std::uint64_t layer_id) noexcept {
    return OpenLayer(context, layer_id, LayerKind::Modal,
                     static_cast<std::uint8_t>(DismissOnOutsideClick | DismissOnEscape));
}

DialogResult AlertDialog(Context* context, std::uint64_t layer_id, const char* title, const char* body,
                         const char* confirm_label, const char* cancel_label) noexcept {
    DialogState* state = State<DialogState>(context, DialogStateId(layer_id));
    if (state == nullptr) return DialogResult::None;
    if (!IsLayerOpen(context, layer_id)) {
        *state = DialogState{};
        return DialogResult::None;
    }
    DialogResult result = DialogResult::None;
    if (!IsLayerClosing(context, layer_id)) {
        state->reported = false;
    } else if (!state->reported) {
        state->reported = true;
        result = state->pending != DialogResult::None ? state->pending : DialogResult::Cancel;
        state->pending = DialogResult::None;
    }

    const Theme theme = GetTheme(context);
    PortalOptions options{};
    options.size = {kDialogWidth, 0.0f};
    options.scrim = true;
    options.offset.y = kDialogRise * (1.0f - GetLayerVisibility(context, layer_id));
    NodeStyle card{};
    card.layout = LayoutMode::Column;
    card.gap = 0.0f;
    card.padding = {kDialogPadding, kDialogPadding, kDialogPadding, kDialogPadding};
    card.radius = kDialogRadius;
    card.background = theme.surface_container_high;
    card.has_background = true;
    card.shadow = &kDialogShadow;
    if (!BeginPortal(context, layer_id, options, card)) return result;
    Label(context, "title", title != nullptr ? title : "", TextStyle(theme.title_size, theme.text));
    NodeStyle body_style = TextStyle(theme.font_size, theme.muted_text);
    body_style.margin.top = 16.0f;
    Label(context, "body", body != nullptr ? body : "", body_style);
    NodeStyle actions{};
    actions.layout = LayoutMode::Row;
    actions.justify_content = Justify::End;
    actions.gap = 8.0f;
    actions.margin.top = 24.0f;
    BeginNode(context, "actions", actions);
    DialogButton(context, "cancel", cancel_label != nullptr ? cancel_label : "Cancel", layer_id, DialogResult::Cancel);
    DialogButton(context, "confirm", confirm_label != nullptr ? confirm_label : "OK", layer_id, DialogResult::Confirm);
    EndNode(context);
    EndPortal(context);
    return result;
}

std::uint64_t ShowToast(ToastQueue& queue, ToastStatus status, const char* title, const char* message,
                        const char* action, double duration_seconds) noexcept {
    if (queue.count == ToastQueue::kCapacity) {
        std::memmove(&queue.entries[0], &queue.entries[1], sizeof(ToastQueue::Entry) * (ToastQueue::kCapacity - 1));
        --queue.count;
    }
    int visible = 0;
    for (int i = 0; i < queue.count; ++i) {
        if (queue.entries[i].exit_start < 0.0 && !queue.entries[i].close_requested) ++visible;
    }
    for (int i = 0; i < queue.count && visible >= ToastQueue::kMaxVisible; ++i) {
        ToastQueue::Entry& old = queue.entries[i];
        if (old.exit_start < 0.0 && !old.close_requested) {
            old.close_requested = true;
            --visible;
        }
    }
    ToastQueue::Entry& entry = queue.entries[queue.count++];
    entry = ToastQueue::Entry{};
    entry.id = queue.next_id++;
    entry.status = status;
    entry.duration = std::isfinite(duration_seconds) && duration_seconds > 0.0 ? duration_seconds : 3.5;
    entry.remaining = entry.duration;
    CopyText(entry.title, sizeof(entry.title), title);
    CopyText(entry.message, sizeof(entry.message), message);
    CopyText(entry.action, sizeof(entry.action), action);
    return entry.id;
}

std::uint64_t Toasts(Context* context, ToastQueue& queue) noexcept {
    const FrameInfo* frame = GetFrameInfo(context);
    if (frame == nullptr) return 0u;
    const double now = frame->time_seconds;
    const double dt = queue.last_time < 0.0 ? 0.0 : std::max(0.0, now - queue.last_time);
    queue.last_time = now;
    const Theme theme = GetTheme(context);
    const bool reduce = IsReduceMotionEnabled(context);

    std::uint64_t action_id = 0u;
    int kept = 0;
    for (int i = 0; i < queue.count; ++i) {
        ToastQueue::Entry& entry = queue.entries[i];
        if (entry.enter_start < 0.0) entry.enter_start = now;
        if (entry.action_requested) {
            entry.action_requested = false;
            entry.close_requested = true;
            if (action_id == 0u) action_id = entry.id;
        }
        if (entry.exit_start < 0.0) {
            if (!entry.hovered) entry.remaining = std::max(0.0, entry.remaining - dt);
            if (entry.close_requested || entry.remaining <= 0.0) entry.exit_start = now;
        }
        float exit_linear = 0.0f;
        if (entry.exit_start >= 0.0) Progress(now, entry.exit_start, theme.exit_transition, reduce, &exit_linear);
        if (entry.exit_start >= 0.0 && exit_linear >= 1.0f) continue;
        if (kept != i) queue.entries[kept] = entry;
        ++kept;
    }
    queue.count = kept;

    if (queue.count == 0) {
        if (IsLayerOpen(context, queue.layer_id) && !IsLayerClosing(context, queue.layer_id)) CloseLayer(context, queue.layer_id);
        return action_id;
    }
    if (!IsLayerOpen(context, queue.layer_id) || IsLayerClosing(context, queue.layer_id)) {
        OpenLayer(context, queue.layer_id, LayerKind::Toast);
    }

    float heights[ToastQueue::kCapacity]{};
    float total = 0.0f;
    for (int i = 0; i < queue.count; ++i) {
        heights[i] = ToastHeight(queue.entries[i], theme);
        total += heights[i] + (i > 0 ? kToastGap : 0.0f);
    }

    const bool top = queue.anchor == ToastAnchor::TopRight;
    const bool slide = queue.anchor != ToastAnchor::BottomCenter;
    PortalOptions options{};
    options.size = {kToastWidth, total};
    options.edge_margin = kToastEdgeMargin;
    options.pin = queue.anchor == ToastAnchor::TopRight ? PortalPin::TopRight
                : queue.anchor == ToastAnchor::BottomRight ? PortalPin::BottomRight : PortalPin::BottomCenter;
    NodeStyle root{};
    root.layout = LayoutMode::Absolute;
    root.padding = {};
    if (!BeginPortal(context, queue.layer_id, options, root)) return action_id;

    // Newest nearest the anchored edge: slots fill from the bottom, or from the top for TopRight.
    float cursor = top ? 0.0f : total;
    for (int i = queue.count - 1; i >= 0; --i) {
        ToastQueue::Entry& entry = queue.entries[i];
        const float slot = top ? cursor : cursor - heights[i];
        cursor = top ? cursor + heights[i] + kToastGap : cursor - heights[i] - kToastGap;
        const float y = Animate(context, MakeId(queue.layer_id, entry.id), slot, theme.reflow_transition);

        float enter_linear = 1.0f;
        const float enter = Progress(now, entry.enter_start, theme.enter_transition, reduce, &enter_linear);
        const float exit = entry.exit_start >= 0.0 ? Clamp(Progress(now, entry.exit_start, theme.exit_transition, reduce), 0.0f, 1.0f) : 0.0f;
        Vec2 offset{};
        if (slide) offset.x = kEnterSlide * (1.0f - enter) + kExitSlide * exit;
        else offset.y = kEnterRise * (1.0f - enter) + kExitDrop * exit;
        const float fade_in = Clamp(enter_linear / kEnterFadePortion, 0.0f, 1.0f);
        DeclareToast(context, queue, entry, theme, {offset.x, y + offset.y}, heights[i], fade_in * (1.0f - exit),
                     Clamp(enter, 0.0f, kBadgeMaxScale));
    }
    EndPortal(context);
    RequestRedraw(context);
    return action_id;
}

} // namespace Lumen::Kit
