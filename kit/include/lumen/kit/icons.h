#ifndef LUMEN_KIT_ICONS_H
#define LUMEN_KIT_ICONS_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

// Stroked vector icons authored on a 24 x 24 grid (Lucide-style geometry).
enum class IconId : std::uint8_t {
    Check, Close, Info, Warning, ChevronDown, ChevronRight, Plus, Minus, Search, Menu, Heart
};

// Draws the icon scaled to the largest square centred in box; stroke_width is in grid units.
void DrawIcon(Canvas& canvas, IconId icon, Rect box, Color color, float stroke_width = 2.0f) noexcept;

// A size x size node that paints the icon; pointer events are off unless the style enables a target.
bool Icon(Context* context, const char* key, IconId icon, float size, Color color, const NodeStyle& style = {}) noexcept;

} // namespace Lumen::Kit

#endif
