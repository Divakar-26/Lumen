#ifndef LUMEN_KIT_TEXT_FIELD_H
#define LUMEN_KIT_TEXT_FIELD_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

// Single-line text field editing a caller-owned, NUL-terminated UTF-8 buffer of capacity bytes.
// Click or Tab focuses it. Typing inserts at the caret, replacing any selection; Left/Right move by
// character (Ctrl: by word) and Shift extends the selection; Home/End; Backspace/Delete (Ctrl: by
// word); Ctrl+A selects all; Ctrl+C/X/V use the host clipboard. Clicking places the caret and
// dragging selects. Long text scrolls horizontally to keep the caret visible. Edits happen during
// EndFrame; the call returns true on the first declaration after the text changed. The style's own
// event handler still runs first; disabled fields show the text but ignore input.
bool TextField(Context* context, const char* key, char* buffer, std::size_t capacity,
               const char* placeholder = nullptr, const NodeStyle& style = {}) noexcept;

} // namespace Lumen::Kit

#endif
