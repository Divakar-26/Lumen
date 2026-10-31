#ifndef LUMEN_KIT_INK_H
#define LUMEN_KIT_INK_H

#include "lumen/lumen.h"

namespace Lumen::Kit {

// Returns a copy of style that answers a primary press with an ink splash: a rounded shape that
// starts 4 x 4 at the press point and grows to the node bounds over Theme::ink_transition while its
// alpha falls from Theme::ink's alpha to zero. tint replaces Theme::ink's colour (not its alpha).
// Only the innermost inked node under the pointer splashes. The style's own event handler and
// custom painter still run (the painter below the ink); calling PreventDefault on PointerDown
// suppresses the splash. Disabled nodes and reduced motion show no ink. The returned style refers to
// frame-arena data, so use it in the current frame. If the arena is exhausted the style is returned
// unchanged.
NodeStyle WithInk(Context* context, const NodeStyle& style, const Color* tint = nullptr) noexcept;

// A container with WithInk(style). Pair a successful call with Lumen::EndNode.
bool BeginInkSurface(Context* context, const char* key, const NodeStyle& style = {}) noexcept;

} // namespace Lumen::Kit

#endif
