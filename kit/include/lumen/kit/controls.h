#ifndef LUMEN_KIT_CONTROLS_H
#define LUMEN_KIT_CONTROLS_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

// A flat tonal card: Theme::surface fill, Theme::radius corners, 18 x 16 padding, column layout.
NodeStyle CardStyle(const Theme& theme) noexcept;

// A labelled on/off switch row. Clicking anywhere on the row, or Enter/Space while it is focused,
// flips *value during EndFrame (the caller sees the new value from the next frame). The thumb moves
// with Theme::toggle_transition; colours follow its clamped progress. A null value or
// style.disabled makes the row inert.
bool Switch(Context* context, const char* key, const char* label, bool* value,
            const NodeStyle& style = {}) noexcept;

// A row of mutually exclusive pill options: the selected one is a Filled button, the rest are
// Outlined. Clicking an option sets *selected to its index during EndFrame.
bool Segmented(Context* context, const char* key, const char* const* labels, int count, int* selected,
               float item_width = 112.0f) noexcept;

} // namespace Lumen::Kit

#endif
