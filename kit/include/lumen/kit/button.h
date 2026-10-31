#ifndef LUMEN_KIT_BUTTON_H
#define LUMEN_KIT_BUTTON_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

enum class ButtonVariant : std::uint8_t { Filled, Tonal, Outlined, Text };

// Mixes content over base by amount (a Material state layer). The result keeps base's alpha.
Color StateLayer(Color base, Color content, float amount) noexcept;

// The variant's look, taken from the current theme: pill shape at Theme::button_min_height, theme
// button padding (14 px sides for Text), state-layer hover/pressed fills and, for Filled and Tonal,
// a hover lift shadow. Adjust the returned style before passing it to Button. The shadow lives in
// the frame arena, so use the style in the current frame.
NodeStyle ButtonStyle(Context* context, ButtonVariant variant) noexcept;

// A core Lumen::Button with the variant's style (or the given one) and an ink splash tinted for
// the variant: Theme::accent_text on Filled, Theme::accent on the others.
bool Button(Context* context, const char* key, const char* text, ButtonVariant variant,
            ClickHandler handler = nullptr, void* user_data = nullptr) noexcept;
bool Button(Context* context, const char* key, const char* text, ButtonVariant variant,
            ClickHandler handler, void* user_data, const NodeStyle& style) noexcept;

} // namespace Lumen::Kit

#endif
