# General node interaction state

M4 follow-up generalizes hover and pressed selectors beyond `Button` nodes.

## Behavior

- Every pointer-enabled node on the target's ancestor path receives `hovered=true` while the pointer is over the target.
- Every pointer-enabled node on the active press target's ancestor path receives `pressed=true` until the press ends, including while a drag moves outside the original hit rectangle.
- Disabled nodes and nodes with `pointer_events=false` do not resolve their own hover/pressed state.
- A non-button container with `has_hover_background` or `has_pressed_background` becomes a hit target for its own empty surface. Descendant controls retain priority because hit testing searches children first.
- A container with only `ShadowStyle::has_hover` is also a hit target, so hover elevation works without requiring a fill selector or dummy event handler.
- `ShadowStyle::has_hover` can elevate any hovered pointer-enabled node.
- All state-dependent fills and shadows continue through the existing `Animate` path and theme transition tokens; no per-node interaction storage was added.

## Query API

```cpp
const Lumen::NodeInteractionState state = Lumen::GetNodeState(context, panel_id);
if (state.found && state.hovered) {
    // The panel or one of its descendants is currently under the pointer.
}
```

`GetNodeState()` reports the most recently completed input/layout pass. Query it after `EndFrame()` and before the next `BeginFrame()`. The tree is frame-scoped, so an ID not present in that completed tree returns `found == false`.

## Regression coverage

`TestContainerInteractionState` verifies:

1. An unhovered container starts inactive.
2. Hovering a child marks both the child and pointer-enabled parent hovered.
3. Pressing the child marks the parent pressed without stealing focus from the child.
4. Dragging outside clears hover but retains pressed state until release.
5. Releasing clears pressed state.
6. An empty region in a styled container can itself be hovered, pressed and focused.
7. Unknown IDs return `found == false`.

`TestHoverShadowMakesContainerPointerTarget` verifies that a surface with only a hover-shadow style becomes hoverable even without a background selector, focusable flag, or event callback.

## Visual evidence

- `screenshots/interaction_hover.png`: the left non-button container is hovered; the neutral surface remains unchanged.
- `screenshots/interaction_pressed.png`: the left non-button container is held; its pressed selector is distinct from hover.

These are interaction diagnostics, not a claim that the current gallery matches the Material 3 toast/dialog reference. Material roles, the Kit target, portals, toast and dialog remain later work items.
