#include "lumen/kit/button.h"
#include "lumen/kit/ink.h"

#include <cmath>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kStateLayerHover = 0.08f;
constexpr float kStateLayerPressed = 0.08f;
constexpr float kOutlinedPressedAlpha = 0.06f;
constexpr float kDisabledContainerAlpha = 0.12f;
constexpr float kTextButtonPadding = 14.0f;
constexpr float kHoverLiftBlur = 12.0f;
constexpr float kHoverLiftOffsetY = 4.0f;

Color WithAlpha(Color color, float alpha) noexcept {
    color.a = alpha;
    return color;
}

} // namespace

Color StateLayer(Color base, Color content, float amount) noexcept {
    const float t = Clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    return Color{Lerp(base.r, content.r, t), Lerp(base.g, content.g, t), Lerp(base.b, content.b, t), base.a};
}

NodeStyle ButtonStyle(Context* context, ButtonVariant variant) noexcept {
    const Theme theme = GetTheme(context);
    NodeStyle style{};
    style.height = theme.button_min_height;
    style.radius = 0.5f * theme.button_min_height;
    style.padding = theme.button_padding;
    style.font_size = theme.font_size;
    style.has_background = true;
    style.has_hover_background = true;
    style.has_pressed_background = true;
    style.has_text_color = true;
    style.disabled_background = WithAlpha(theme.text, kDisabledContainerAlpha);
    style.has_disabled_background = true;

    switch (variant) {
        case ButtonVariant::Filled:
        case ButtonVariant::Tonal: {
            const bool filled = variant == ButtonVariant::Filled;
            const Color container = filled ? theme.accent : theme.secondary_container;
            const Color content = filled ? theme.accent_text : theme.on_secondary_container;
            style.background = container;
            style.hover_background = StateLayer(container, content, kStateLayerHover);
            style.pressed_background = StateLayer(container, content, kStateLayerPressed);
            style.text_color = content;
            void* memory = FrameAlloc(context, sizeof(ShadowStyle), alignof(ShadowStyle));
            if (memory != nullptr) {
                auto* lift = new (memory) ShadowStyle{};
                lift->has_hover = true;
                lift->hover_blur = kHoverLiftBlur;
                lift->hover_offset_y = kHoverLiftOffsetY;
                style.shadow = lift;
            }
            break;
        }
        case ButtonVariant::Outlined:
        case ButtonVariant::Text:
            style.background = WithAlpha(theme.accent, 0.0f);
            style.hover_background = WithAlpha(theme.accent, kStateLayerHover);
            style.pressed_background = WithAlpha(theme.accent, kOutlinedPressedAlpha);
            style.text_color = theme.accent;
            if (variant == ButtonVariant::Outlined) {
                style.border_width = 1.0f;
                style.border_color = theme.border;
                style.has_border_color = true;
            } else {
                style.padding.left = kTextButtonPadding;
                style.padding.right = kTextButtonPadding;
            }
            break;
    }
    return style;
}

bool Button(Context* context, const char* key, const char* text, ButtonVariant variant,
            ClickHandler handler, void* user_data) noexcept {
    return Button(context, key, text, variant, handler, user_data, ButtonStyle(context, variant));
}

bool Button(Context* context, const char* key, const char* text, ButtonVariant variant,
            ClickHandler handler, void* user_data, const NodeStyle& style) noexcept {
    const Theme theme = GetTheme(context);
    const Color tint = variant == ButtonVariant::Filled ? theme.accent_text : theme.accent;
    return Lumen::Button(context, key, text, handler, user_data, WithInk(context, style, &tint));
}

} // namespace Lumen::Kit
