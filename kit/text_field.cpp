#include "lumen/kit/text_field.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kFieldHeight = 48.0f;
constexpr float kFieldRadius = 12.0f;
constexpr float kFieldPadding = 16.0f;
constexpr float kCaretWidth = 2.0f;
constexpr float kSelectionAlpha = 0.28f;
constexpr double kBlinkPeriod = 1.0;
constexpr double kBlinkOn = 0.55;

struct FieldState {
    std::uint32_t caret = 0u;
    std::uint32_t anchor = 0u;
    float scroll = 0.0f;
    double blink_start = 0.0;
    bool changed = false;
};

struct FieldData {
    char* buffer = nullptr;
    std::size_t capacity = 0u;
    const char* placeholder = nullptr;
    EventHandler handler = nullptr;
    void* handler_data = nullptr;
    float font_size = 16.0f;
    float padding = kFieldPadding;
    float radius = kFieldRadius;
    bool disabled = false;
};

std::uint64_t StateId(std::uint64_t node_id) noexcept { return MakeId(node_id, "kit-field"); }

double Now(const Context* context) noexcept {
    const FrameInfo* frame = GetFrameInfo(context);
    return frame != nullptr ? frame->time_seconds : 0.0;
}

bool IsContinuation(char c) noexcept { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; }

bool IsWordByte(char c) noexcept {
    const auto byte = static_cast<unsigned char>(c);
    return byte >= 0x80u || (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'z') ||
           (byte >= 'A' && byte <= 'Z') || byte == '_';
}

std::size_t Snap(const char* text, std::size_t length, std::size_t index) noexcept {
    index = std::min(index, length);
    while (index > 0u && index < length && IsContinuation(text[index])) --index;
    return index;
}

std::size_t Next(const char* text, std::size_t length, std::size_t index) noexcept {
    if (index >= length) return length;
    ++index;
    while (index < length && IsContinuation(text[index])) ++index;
    return index;
}

std::size_t Prev(const char* text, std::size_t index) noexcept {
    if (index == 0u) return 0u;
    --index;
    while (index > 0u && IsContinuation(text[index])) --index;
    return index;
}

std::size_t WordLeft(const char* text, std::size_t index) noexcept {
    while (index > 0u && !IsWordByte(text[index - 1u])) index = Prev(text, index);
    while (index > 0u && IsWordByte(text[index - 1u])) index = Prev(text, index);
    return index;
}

std::size_t WordRight(const char* text, std::size_t length, std::size_t index) noexcept {
    while (index < length && IsWordByte(text[index])) index = Next(text, length, index);
    while (index < length && !IsWordByte(text[index])) index = Next(text, length, index);
    return index;
}

std::size_t EncodeUtf8(std::uint32_t codepoint, char* out) noexcept {
    if (codepoint < 0x80u) {
        out[0] = static_cast<char>(codepoint);
        return 1u;
    }
    if (codepoint < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (codepoint >> 6));
        out[1] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        return 2u;
    }
    if (codepoint < 0x10000u) {
        if (codepoint >= 0xD800u && codepoint <= 0xDFFFu) return 0u;
        out[0] = static_cast<char>(0xE0u | (codepoint >> 12));
        out[1] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        return 3u;
    }
    if (codepoint > 0x10FFFFu) return 0u;
    out[0] = static_cast<char>(0xF0u | (codepoint >> 18));
    out[1] = static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu));
    out[2] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
    out[3] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
    return 4u;
}

void SetCaret(FieldState* state, std::size_t caret, bool extend) noexcept {
    state->caret = static_cast<std::uint32_t>(caret);
    if (!extend) state->anchor = state->caret;
}

bool DeleteRange(char* text, std::size_t& length, FieldState* state, std::size_t from, std::size_t to) noexcept {
    if (from >= to) return false;
    std::memmove(text + from, text + to, length - to + 1u);
    length -= to - from;
    SetCaret(state, from, false);
    return true;
}

bool DeleteSelection(char* text, std::size_t& length, FieldState* state) noexcept {
    return DeleteRange(text, length, state, std::min(state->caret, state->anchor), std::max(state->caret, state->anchor));
}

// Inserts the printable characters of [source, source + count) at the caret, replacing the selection
// and truncating at a character boundary when the buffer is full.
bool Insert(const FieldData* data, std::size_t& length, FieldState* state, const char* source, std::size_t count) noexcept {
    bool changed = DeleteSelection(data->buffer, length, state);
    std::size_t caret = state->caret;
    std::size_t index = 0u;
    while (index < count && source[index] != '\0') {
        std::size_t end = index + 1u;
        while (end < count && IsContinuation(source[end])) ++end;
        const std::size_t size = end - index;
        const auto lead = static_cast<unsigned char>(source[index]);
        if (lead >= 0x20u && lead != 0x7Fu) {
            if (length + size + 1u > data->capacity) break;
            std::memmove(data->buffer + caret + size, data->buffer + caret, length - caret + 1u);
            std::memcpy(data->buffer + caret, source + index, size);
            caret += size;
            length += size;
            changed = true;
        }
        index = end;
    }
    SetCaret(state, caret, false);
    return changed;
}

float PrefixWidth(const Context* context, const FieldData* data, std::size_t end) noexcept {
    return end == 0u ? 0.0f : MeasureText(context, data->buffer, data->font_size, 0.0f, data->buffer + end).x;
}

std::size_t HitIndex(const Context* context, const FieldData* data, const FieldState* state, const Rect& rect,
                     float x) noexcept {
    const std::size_t length = std::strlen(data->buffer);
    const float local = x - (rect.min.x + data->padding) + state->scroll;
    std::size_t index = 0u;
    float previous = 0.0f;
    while (index < length) {
        const std::size_t next = Next(data->buffer, length, index);
        const float width = PrefixWidth(context, data, next);
        if (local < 0.5f * (previous + width)) return index;
        previous = width;
        index = next;
    }
    return length;
}

void HandleFieldEvent(Context* context, UIEvent& event, void* user_data) noexcept {
    const auto* data = static_cast<const FieldData*>(user_data);
    if (data->handler != nullptr) data->handler(context, event, data->handler_data);
    if (data->disabled || event.phase != UIEventPhase::Target || event.default_prevented) return;
    const std::uint64_t id = event.current_target_id;
    FieldState* state = State<FieldState>(context, StateId(id));
    if (state == nullptr) return;
    char* text = data->buffer;
    std::size_t length = std::strlen(text);
    state->caret = static_cast<std::uint32_t>(Snap(text, length, state->caret));
    state->anchor = static_cast<std::uint32_t>(Snap(text, length, state->anchor));
    const bool shift = (event.modifiers & KeyModShift) != 0u;
    const bool command = (event.modifiers & (KeyModCtrl | KeyModSuper)) != 0u;
    bool handled = true;
    bool changed = false;

    switch (event.type) {
        case UIEventType::PointerDown: {
            Rect rect{};
            if (!GetNodeRect(context, id, &rect)) return;
            SetCaret(state, HitIndex(context, data, state, rect, event.position.x), shift);
            CapturePointer(context, id, event.pointer_id);
            break;
        }
        case UIEventType::PointerMove: {
            Rect rect{};
            if (!HasPointerCapture(context, id, event.pointer_id) || !GetNodeRect(context, id, &rect)) return;
            SetCaret(state, HitIndex(context, data, state, rect, event.position.x), true);
            break;
        }
        case UIEventType::PointerUp:
            if (HasPointerCapture(context, id, event.pointer_id)) ReleasePointer(context, id, event.pointer_id);
            return;
        case UIEventType::TextInput: {
            char encoded[4];
            const std::size_t size = EncodeUtf8(event.codepoint, encoded);
            changed = Insert(data, length, state, encoded, size);
            break;
        }
        case UIEventType::KeyDown: {
            const std::size_t low = std::min(state->caret, state->anchor);
            const std::size_t high = std::max(state->caret, state->anchor);
            switch (event.key) {
                case ImGuiKey_LeftArrow:
                    if (!shift && low != high) SetCaret(state, low, false);
                    else SetCaret(state, command ? WordLeft(text, state->caret) : Prev(text, state->caret), shift);
                    break;
                case ImGuiKey_RightArrow:
                    if (!shift && low != high) SetCaret(state, high, false);
                    else SetCaret(state, command ? WordRight(text, length, state->caret) : Next(text, length, state->caret), shift);
                    break;
                case ImGuiKey_Home: SetCaret(state, 0u, shift); break;
                case ImGuiKey_End: SetCaret(state, length, shift); break;
                case ImGuiKey_Backspace:
                    changed = low != high ? DeleteSelection(text, length, state)
                        : DeleteRange(text, length, state, command ? WordLeft(text, low) : Prev(text, low), low);
                    break;
                case ImGuiKey_Delete:
                    changed = low != high ? DeleteSelection(text, length, state)
                        : DeleteRange(text, length, state, low, command ? WordRight(text, length, low) : Next(text, length, low));
                    break;
                case ImGuiKey_A:
                    if (!command) return;
                    state->anchor = 0u;
                    state->caret = static_cast<std::uint32_t>(length);
                    break;
                case ImGuiKey_C:
                case ImGuiKey_X: {
                    if (!command) return;
                    if (low == high) break;
                    auto* copy = static_cast<char*>(FrameAlloc(context, high - low + 1u, 1u));
                    if (copy == nullptr) break;
                    std::memcpy(copy, text + low, high - low);
                    copy[high - low] = '\0';
                    SetClipboardText(context, copy);
                    if (event.key == ImGuiKey_X) changed = DeleteSelection(text, length, state);
                    break;
                }
                case ImGuiKey_V: {
                    if (!command) return;
                    const char* clip = GetClipboardText(context);
                    if (clip != nullptr) changed = Insert(data, length, state, clip, std::strlen(clip));
                    break;
                }
                case ImGuiKey_Enter:
                case ImGuiKey_KeypadEnter:
                case ImGuiKey_Space:
                    // Space arrives as TextInput; neither key should activate the field as a click.
                    break;
                default:
                    handled = false;
                    break;
            }
            break;
        }
        default:
            handled = false;
            break;
    }
    if (!handled) return;
    if (event.type == UIEventType::KeyDown) event.PreventDefault();
    if (changed) state->changed = true;
    state->blink_start = Now(context);
    RequestRedraw(context);
}

void PaintField(Context* context, Canvas& canvas, void* user_data) noexcept {
    const auto* data = static_cast<const FieldData*>(user_data);
    FieldState* state = State<FieldState>(context, StateId(canvas.NodeId()));
    if (state == nullptr) return;
    const Theme theme = GetTheme(context);
    const NodeInteractionState interaction = GetNodeState(context, canvas.NodeId());
    const bool focused = interaction.focused && !data->disabled;
    const char* text = data->buffer;
    const std::size_t length = std::strlen(text);
    const std::size_t caret = Snap(text, length, state->caret);
    const std::size_t anchor = Snap(text, length, state->anchor);

    const Rect bounds = canvas.Bounds();
    const Rect inner{{bounds.min.x + data->padding, bounds.min.y}, {bounds.max.x - data->padding, bounds.max.y}};
    const float line = MeasureText(context, "Ag", data->font_size).y;
    const float top = bounds.min.y + 0.5f * (bounds.Height() - line);

    // Keep the caret inside the visible width.
    const float view = std::max(0.0f, inner.Width() - kCaretWidth);
    const float caret_x = PrefixWidth(context, data, caret);
    const float total = PrefixWidth(context, data, length);
    if (caret_x - state->scroll > view) state->scroll = caret_x - view;
    if (caret_x < state->scroll) state->scroll = caret_x;
    state->scroll = Clamp(state->scroll, 0.0f, std::max(0.0f, total - view));
    const float origin = inner.min.x - state->scroll;

    if (focused) {
        const float inset = 1.0f;
        canvas.StrokeRect(Rect{{bounds.min.x + inset, bounds.min.y + inset}, {bounds.max.x - inset, bounds.max.y - inset}},
                          theme.accent, data->radius - inset, 2.0f);
    } else {
        const float inset = 0.5f;
        canvas.StrokeRect(Rect{{bounds.min.x + inset, bounds.min.y + inset}, {bounds.max.x - inset, bounds.max.y - inset}},
                          interaction.hovered && !data->disabled ? theme.text : theme.border, data->radius - inset, 1.0f);
    }

    canvas.PushClip(inner);
    if (focused && caret != anchor) {
        const float x0 = origin + PrefixWidth(context, data, std::min(caret, anchor));
        const float x1 = origin + PrefixWidth(context, data, std::max(caret, anchor));
        Color selection = theme.accent;
        selection.a = kSelectionAlpha;
        canvas.FillRect(Rect{{x0, top}, {x1, top + line}}, selection);
    }
    if (length > 0u) {
        canvas.Text({origin, top}, text, data->disabled ? theme.muted_text : theme.text, data->font_size);
    } else if (data->placeholder != nullptr) {
        canvas.Text({inner.min.x, top}, data->placeholder, theme.muted_text, data->font_size);
    }
    if (focused) {
        const double phase = std::fmod(std::max(0.0, Now(context) - state->blink_start), kBlinkPeriod);
        if (phase < kBlinkOn) canvas.FillRect(Rect{origin + caret_x, top, kCaretWidth, line}, theme.accent);
        RequestRedraw(context);
    }
    canvas.PopClip();
}

} // namespace

bool TextField(Context* context, const char* key, char* buffer, std::size_t capacity, const char* placeholder,
               const NodeStyle& style) noexcept {
    if (buffer == nullptr || capacity == 0u) return false;
    buffer[capacity - 1u] = '\0';
    void* memory = FrameAlloc(context, sizeof(FieldData), alignof(FieldData));
    if (memory == nullptr) return false;
    const Theme theme = GetTheme(context);
    auto* data = new (memory) FieldData{};
    data->buffer = buffer;
    data->capacity = capacity;
    data->placeholder = placeholder;
    data->handler = style.event_handler;
    data->handler_data = style.event_user_data;
    data->font_size = style.font_size > 0.0f ? style.font_size : theme.font_size;
    data->padding = style.padding.left > 0.0f ? style.padding.left : kFieldPadding;
    data->radius = style.radius >= 0.0f ? style.radius : kFieldRadius;
    data->disabled = style.disabled;
    const CustomPaint paint{PaintField, data};

    NodeStyle node = style;
    if (node.height < 0.0f && node.min_height <= 0.0f) node.height = kFieldHeight;
    node.radius = data->radius;
    if (!node.has_background) {
        node.background = theme.canvas;
        node.has_background = true;
    }
    node.focusable = !data->disabled;
    node.event_handler = HandleFieldEvent;
    node.event_user_data = data;
    node.custom_paint = &paint;
    if (!BeginNode(context, key, node)) return false;
    FieldState* state = State<FieldState>(context, StateId(GetCurrentNodeId(context)));
    EndNode(context);
    if (state == nullptr || !state->changed) return false;
    state->changed = false;
    return true;
}

} // namespace Lumen::Kit
