# Layers, portals and overlay behavior

The layer system is integrated with `Lumen::Context`. `LayerStack` remains the backend-independent, fixed-capacity z-order registry; `BeginPortal()`/`EndPortal()` declare a detached per-frame subtree for an active layer. Lumen lays that subtree out against the frame viewport, resolves its anchor, paints it after the base tree in layer paint order, and routes pointer/keyboard input through the active overlay stack.

## Public API

The public interface is in `include/lumen/ui.h`:

- `OpenLayer(context, id, kind, dismissal_policy, timeout_seconds)` creates or reopens a layer and starts its Presence enter transition.
- `CloseLayer(context, id)` begins its Presence exit transition. The layer remains mounted and continues to block input while exiting.
- `IsLayerOpen(context, id)` reports whether the layer remains registered. It therefore remains `true` during its exit transition, and becomes `false` after the exit finishes.
- `BeginPortal(context, id, options, style)` starts the detached node root for the current frame. It must be paired with `EndPortal(context)` on every successful begin.

A layer ID must be non-zero and unique while active. The fixed layer/portal capacity is 32. Reopening a layer while it is exiting reverses the Presence transition from its current opacity, rather than restarting from a hard-coded endpoint. A positive `timeout_seconds` is honored only when `DismissOnTimeout` is present in the dismissal policy. Timeout expiration starts the same animated exit path as an explicit close.

## Declaring a portal

Declare the normal anchor node before declaring its portal in the same frame. The anchor ID is a normal node ID (`MakeId(parent_id, key)`). An `anchor_node_id` of zero centers the portal in the safe viewport; `pin_to_top_right` places it at the safe area's top-right edge.

```cpp
constexpr std::uint64_t kAccountPopover = 0xA110u;

void OnAccountMenu(Lumen::Context* ui, std::uint64_t, void*) {
    (void)Lumen::OpenLayer(ui, kAccountPopover, Lumen::LayerKind::Popover,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape));
}

void BuildAccountMenu(Lumen::Context* ui) {
    const auto* frame = Lumen::GetFrameInfo(ui);
    if (frame == nullptr) return;

    Lumen::NodeStyle anchor{};
    anchor.width = 152.0f;
    anchor.height = 40.0f;
    (void)Lumen::Button(ui, "account-anchor", "Account", OnAccountMenu, nullptr, anchor);

    if (!Lumen::IsLayerOpen(ui, kAccountPopover)) return;

    Lumen::PortalOptions options{};
    options.anchor_node_id = Lumen::MakeId(frame->root_id, "account-anchor");
    options.size = {240.0f, 132.0f};
    options.placement = Lumen::Placement::BelowAnchor;
    options.align = Lumen::PlacementAlign::End;
    options.offset = {0.0f, 8.0f};

    Lumen::NodeStyle panel{};
    panel.padding = {12.0f, 12.0f, 12.0f, 12.0f};
    panel.gap = 8.0f;
    panel.has_background = true;
    panel.background = Lumen::GetTheme(ui).surface;

    if (Lumen::BeginPortal(ui, kAccountPopover, options, panel)) {
        (void)Lumen::Label(ui, "menu-title", "Your account");
        (void)Lumen::Button(ui, "settings", "Settings");
        Lumen::EndPortal(ui);
    }
}

```

`BeginPortal()` may be called from an ordinary node-building scope. The portal's visual layout is detached from its declaration parent and resolved against the viewport; the declaration scope is restored by `EndPortal()`. Nested portals are currently rejected. Anchors must be nodes in the base tree (portal-to-portal anchoring is not implemented). A declaration is required every frame while `IsLayerOpen()` remains true, including the exit transition, so the content stays alive until Presence completes.

## Placement

`PortalOptions` supports:

- `anchor_node_id`: base-tree anchor, or zero for a centered viewport portal.
- `size`: desired portal root size.
- `placement` and `align`: `Auto`, `AboveAnchor`, `BelowAnchor`, `Leading`, `Trailing` and start/center/end alignment.
- `offset` and `flip`: gap and preferred-side fallback.
- `clamp_to_safe_area` and `edge_margin`: viewport collision handling.
- `scrim`: draws a themed viewport scrim immediately before portal content; accepted only for `LayerKind::Modal`.
- `pin_to_top_right`: safe-area pinned toast placement; mutually exclusive with an anchor.

Anchored portals use the anchor's visible, clipped rectangle. If the anchor is completely clipped out, the portal is omitted for that frame rather than floating at a stale position. Base-tree scroll relayouts the anchor and portal in the same frame. Portal contents are constrained to the resolved root size and keep their normal row/column/stack/grid layout behavior.

## Paint and input ordering

The base tree paints first. Active portal roots then paint in `LayerStack::CopyPaintOrder()` order; a modal scrim is emitted immediately before its modal root. The whole operation remains inside the host-owned ImGui draw list and viewport clip. Lumen creates no OS windows, GPU objects or renderer resources.

Hit testing checks active portals from topmost to bottommost before considering the base tree. Scroll hit-testing follows the same order. A modal layer blocks lower layers and the base tree, including the pointer-down that dismisses it; the same click cannot leak through and activate a background control. The top active layer's dismissal policy is used for outside-click and Escape handling. LIFO dismissal semantics are preserved: a newer layer that does not opt into a dismissal reason prevents dismissal of older layers underneath it.

Non-modal portal controls participate in ordinary Tab/Shift+Tab traversal after base controls. The top modal creates an isolated focus scope; traversal wraps within that scope. When the modal closes, focus returns to the previously focused base or portal control where that node still exists. `GetNodeRect()` and `GetNodeState()` can query portal nodes by ID after `EndFrame()` and before the next `BeginFrame()`.

## Presence and redraw demand

Every Context-managed portal has a fixed-capacity `Presence` runtime. Opening starts an enter transition, closing and timeout expiration start an exit transition, and reopening during exit reverses from the current progress. Exit content remains declared, painted and input-gated until the exit finishes; then the layer entry and runtime are removed. The current transition uses the theme's color-transition duration and curve. Reduced-motion mode removes the transition and requests a repaint immediately.

`Lumen::NeedsFrame(context)` remains true while a portal is transitioning or waiting for a scheduled timeout. A host should continue calling `BeginFrame()` while this returns true, even if no input arrives, so transitions and timed layers can complete.

## Limits in this milestone

- One base-tree anchor per portal; portal-to-portal anchors are not supported.
- Nested `BeginPortal()` scopes are rejected; a portal builder must balance its nodes and call `EndPortal()`.
- The fixed layer/portal capacity is 32 per context.
- One ImGui mouse pointer is supported; multitouch and gesture arenas are not implemented.
- Focus support is modal-scope Tab traversal and restoration, not a complete accessibility/focus-scope API.
- Toast layout/stacking, countdown pause-on-hover, and Material Kit components are not part of core; they belong in the future `lumen_kit` target.
