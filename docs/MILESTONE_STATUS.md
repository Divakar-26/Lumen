# Lumen implementation status

This repository follows the frozen master plan in `Lumen_UI_Engine_Master_Plan_No_Section_Signs.md`. The previous standalone tween playground is not part of the engine implementation.

## M0 — Foundation: implemented, Linux-validated

- `lumen::core` static library target, vendored `imgui::imgui` development target, and host-owned ImGui context contract.
- Frame protocol, redraw demand, fixed-capacity frame arena, open-addressed state pool with typed access, pinning and TTL eviction.
- Stable IDs, geometry primitives, real/manual clocks and input event injection.
- `core/imgui_bridge.cpp` is the only Lumen translation unit that includes `imgui_internal.h`.
- Tests cover lifecycle, frames, IDs, geometry, state persistence/eviction, input scheduling, arena alignment and exhaustion, and context/frame ownership.

## M1 — Motion: core exit tests pass on Linux

- Easing families, cubic Bézier, steps and custom curves.
- Typed tween/spring support for scalar, geometry and visual types; OKLab color tween interpolation.
- Fixed-capacity keyframes and timeline scheduling; controller play/reverse/pause/resume/seek/repeat/ping-pong/speed/delay/callbacks.
- Per-context controller scheduler, motion time scale, reduce motion, and state-backed implicit animations.
- Tests cover spring retarget continuity, frame-rate independence at 30/60/144 Hz and stopping redraw demand after animation settles.
- Hardening in this revision: non-finite custom-curve output now falls back to linear progress, preventing NaN/Inf values from propagating into animation state. Overshooting custom curves remain supported.


## M0/M1 follow-up hardening — current revision

- Theme changes now use interruptible appearance transitions: palette colors interpolate in OKLab, size/spacing tokens interpolate linearly, and the target theme supplies the curve/duration.
- `IsThemeTransitioning()` exposes the transition state. `SetReduceMotion(true)` finishes an active theme change and requests a repaint. A theme set before the first clocked frame is applied immediately so an uninitialized clock cannot skip the transition unpredictably.
- Viewport bounds and display scale are validated before `BeginFrame` mutates context state. Invalid dimensions/scales reject the frame; safe-area insets are normalized and clamped to avoid negative content rectangles.
- `Router` now bounds route-name scanning to its fixed-capacity name buffer and uses overlap-safe copying for `Replace(Current())`.
- Deterministic tests cover appearance interpolation, transition interruption, reduced-motion completion/redraw, invalid viewport rejection and safe-area clamping.

## M2 — Nodes, style and layout: substantial first implementation, not plan-complete

- Frame-scoped node tree, stable parent-scoped keys, duplicate sibling ID rejection and inherited text/font/opacity.
- Built-in Dark, Light and High Contrast themes. Theme provides spacing, radii, button padding/minimum height, focus-ring metrics, wheel multiplier, tween transition tokens and layout spring parameters.
- Row/column flex layout with grow/shrink, alignment, justification and gaps; stack, absolute and uniform-column grid.
- Fixed-height `Collection` virtualizes rows with visible-window caching and stable index-keyed wrappers. It preserves a total scroll extent even when only a small subset of items is declared.
- Implemented in the latest revision: `flex_basis`, row flex wrapping, `GetNodeRect`, minimum hit targets/per-node hit slop, and nested wheel-scroll chaining.
- Not yet implemented: full CSS-style constraint solving, grid track sizing/spans/areas, flow/custom layout delegates, responsive query API, virtual variable-height rows, anchors and sticky content.

## M3 — Paint: Tier-0 basics implemented, not plan-complete

- Backend-free frame-arena render command list translated to `ImDrawList`.
- Rounded filled rectangles, borders, text, nested clipping, opacity, text wrapping/alignment, animated colors and a keyboard focus outline.
- Implemented: host-provided image handles with Stretch/Contain/Cover fit modes and rounded image draw commands.
- Implemented: drop shadows (`ShadowStyle`: blur, spread, offsets, themed colour, animated pointer-target hover lift) as a Tier-0 `ImDrawList` effect. Known limits: removing a shadow snaps it off instead of fading; translucent nodes show their shadow underneath (no inner clipping); hover lift now applies to pointer-enabled nodes with hover styling; general interaction state selectors were added in the M4 follow-up below.
- Not yet implemented: gradients, per-corner radii/smoothing, full text shaping/overflow policies, font provider and resource cache.

## M4 — Input: core pointer and keyboard support, incomplete against plan

- Pointer hit testing against the current animated rectangles; hover/pressed/disabled visual states; click callback dispatch.
- Typed capture/target/bubble event propagation with `StopPropagation` and `PreventDefault`; pointer capture routes drag move/up events outside bounds.
- Tab/Shift+Tab focus traversal, Enter/Space activation, focus gained/lost events and visible keyboard focus.
- Minimum hit targets, per-node hit slop, wheel-driven scroll offsets, nested clipping and scroll chaining to parent containers.
- Not yet implemented: touch gestures/gesture arena, drag-and-drop, focus scopes, text editing, shortcut dispatch, accessibility tree, scroll physics/inertia and multi-pointer input.

- Added `Image()` as a host-owned resource seam: texture handle, source dimensions and fit mode are passed in; the engine emits ImDrawList image commands and a themed placeholder for null handles. Contain preserves aspect ratio, Cover crops UVs, and Stretch uses the full target. The host retains all GPU ownership.

## Memory budget note (found while adding shadows)

The default 1 MiB frame arena is nearly fully used by 2,000 plain labels (1,040,720 bytes before shadows, about 520 bytes per node: a 432-byte `Node` that embeds a full 256-byte `NodeStyle`, a 72-byte `RenderCommand`, and copied text). `NodeStyle` members are now ordered by size class so the shadow pointer costs no extra bytes, and `test_ui` keeps asserting the budget. Shrinking `Node` (hot/cold split) or a chunked arena is the next structural fix.

## M5 — Layers and behaviors: behavior primitives started (integration delivered later in M8)

- Added a bounded `Router` with copied route names, push/replace/pop, current/previous route inspection, and explicit capacity/name-length rejection.
- Added interruptible `Presence` enter/exit transitions with opacity continuity when reversed, zero-duration handling, monotonic-time protection, and `NeedsFrame()` state.
- At the M5 milestone these were standalone deterministic behavior primitives; the Context-integrated layer/portal work is recorded under M8 below. Viewport, shared-element transitions and animated layout remain outstanding. M5 was partial at that point in the sequence.

## Sample application

`lumen_gallery` is a real runnable Linux X11/OpenGL host using the public engine API. It displays interactive buttons, theme switching, grid-based dashboard cards, theme token swatches and a virtualized activity feed. The host/backend is optional and not linked into `lumen::core`.

## Validation and remaining gates

- Linux Debug build and CTest pass.
- Tests exercise click dispatch, capture/target/bubble order, stop/prevent semantics, pointer capture outside node bounds, flex wrapping/basis, computed geometry, minimum hit targets, nested scroll chaining, keyboard focus/activation, grid construction at 2,000 nodes and virtual collection at 100,000 logical items.
- AddressSanitizer and UndefinedBehaviorSanitizer validation is run as part of this revision before packaging.
- Windows/macOS builds, CI for multiple ImGui versions, and formal acceptance of all M2/M3/M4 requirements remain outstanding.


## M6 — Layer foundations and quality hardening: implemented primitives, integration still partial

- Added header-only `LayerStack` with fixed capacity, stable z-order groups, activation ordering, duplicate-ID refresh semantics, explicit overflow/rejection, topmost lookup, LIFO dismissal policies, timeout expiry and host-facing `NeedsFrame()`.
- Added `BlocksInput()` modal gating query and `CopyPaintOrder()` for allocation-free paint ordering.
- Added pure `ResolveAnchorPlacement()` with safe-area geometry, Above/Below/Leading/Trailing/Auto, start/center/end alignment, preferred-side flipping and clamping.
- Hardened M1 `Presence`: non-finite custom easing no longer contaminates opacity; it falls back to linear progress.
- Improved gallery layout by using a viewport-sized clipped scrolling root; the growing showcase no longer compresses rows/cards into the viewport. Added an interactive layer-registry/placement diagnostics panel.
- Added `lumen_layer_tests` covering paint order, capacity, reopen semantics, LIFO dismissal, modal input gating, monotonic timeout, safe-area placement, flip/clamp behavior and invalid geometry.
- At the M6 checkpoint, `Portal` integration, automatic layer render/hit-test routing, modal focus trapping/restoration, scrim rendering and input-driven dismissal remained outstanding. These were implemented in the M8 follow-up below.

## M4 follow-up — general interaction selectors: implemented, tested

- Hovered and pressed state is now resolved for every pointer-enabled node on the hit/active ancestry, not only `Button` nodes. A child interaction also exposes the parent container's hover/pressed state, while `pointer_events=false` and disabled nodes opt out.
- Containers with hover/pressed selectors are direct hit targets in otherwise empty surface areas, so cards and custom surfaces can react without a dummy Button or event callback. Children retain hit-test priority.
- `ShadowStyle::has_hover` elevation now applies to any hovered pointer-enabled node, not only buttons.
- Added `NodeInteractionState` and `GetNodeState(context, id)`, which reports found/hovered/pressed/focused/focus-visible for the last completed frame. The query remains valid after `EndFrame` until the next `BeginFrame` rebuilds the frame-scoped tree.
- No per-node fields were added; the memory budget is unchanged by this feature.
- New regression tests: `TestContainerInteractionState` covers hover bubbling to a parent container, child-to-parent pressed propagation, drag-out behavior, direct hit-testing on empty card space, focus state, and missing IDs; `TestHoverShadowMakesContainerPointerTarget` verifies hover-only shadow surfaces are independently hit-testable.

## Visual acceptance tooling

- Added `lumen_interaction_screenshot` (headless; no X11 or GPU) using `tools/headless_raster.h`. It emits hover and pressed container screenshots from a `ManualClock` driven scene. The tool loads the bundled Roboto Medium face when run from the repository root.
- This note described the M6/M7 interaction-only upload before the M8 portal integration. See the M8 section below for the current source status.


## M8 — Integrated portals: first end-to-end implementation, bounded and partial

- Added public Context APIs `OpenLayer`, `CloseLayer`, `IsLayerOpen`, `BeginPortal` and `EndPortal`. The gallery now uses these APIs instead of maintaining a separate diagnostic-only `LayerStack`.
- Portal roots live in the fixed-capacity Context portal table rather than adding fields to every `Node`; the existing 1 MiB frame-arena budget and per-node footprint remain unchanged.
- Each active portal is independently laid out against the viewport/safe area. Anchored placement uses the current visible/clipped base-tree anchor rect, supports flipping/clamping and is recomputed after base scroll. Unanchored portals can center or pin to the top-right safe-area edge.
- The base tree paints first; portal roots paint afterwards in `LayerStack` z/activation order. Modal scrims are drawn immediately before their modal content. Portals are hit-tested before base content and scroll routing follows the same overlay order.
- Modal layers gate background pointer and keyboard input, isolate Tab/Shift+Tab traversal, and restore the previous focus target when the modal closes. Escape/outside-click use the configured dismissal policy; the dismissal click cannot leak through to a base control.
- Per-layer `Presence` drives enter, exit and timeout transitions. Exiting content remains declared/mounted until its transition completes; reopening while exiting reverses continuously. `NeedsFrame()` requests host frames during transitions and pending timeout deadlines. Reduced-motion mode finishes transitions immediately.
- `GetNodeRect()` and `GetNodeState()` can query portal descendants after the frame. Pointer capture, normal event routing and buttons work inside portal trees.
- Tests in `lumen_ui_tests`: `TestPortalPaintOrderIsAboveBaseTree`, `TestPortalPaintingAnchorAndDismissal`, `TestModalFocusTrapScrimAndRestore`, `TestPinnedPortalTracksSafeViewport`, `TestPortalPresenceLifecycle`, `TestPortalTimeoutUsesExitTransition`. These cover portal draw output, anchored geometry, Tab/Enter activation, outside/Escape dismissal, modal input isolation and focus restoration, safe-viewport pinning, and exit retention.
- Gallery launch flags `--popover`, `--modal` and `--toast` produce deterministic portal states for visual checks. Default, popover, modal and toast runtime screenshots are captured from the compiled X11/OpenGL gallery.
- Limits: 32 layers/portals per context; nested portals and portal-to-portal anchors are rejected; anchor IDs must resolve in the base tree; focus handling is a modal Tab scope rather than a complete accessibility focus manager; one mouse pointer only. Kit widgets, animated layout, toast stacking/countdown pause, and cross-platform validation remain unfinished.
- Status: the required first portal integration is implemented and Linux-tested; full plan item 2 remains **partial** because the API is `BeginPortal`/`EndPortal` (not a callback/RAII builder), nested portal trees and portal anchors are not supported, and focus/accessibility is not yet a complete generalized scope system.

## Custom paint and Kit ink surface — implemented, tested

- Plan 6.9 painter hook: `NodeStyle::custom_paint` points to a `CustomPaint` (painter + user data) that `BeginNode` copies into the frame arena, like `shadow`. A `Paint` render command (pointing back at its node, so `RenderCommand` stays 72 bytes) invokes the painter with a `Canvas` after the node's shadow and fill, before its border, focus ring, image, text and children, inside the active clip.
- `Canvas` provides node ID, bounds, resolved opacity, opacity-aware rect/circle/line/text primitives, intersecting clip push/pop with automatic cleanup, and raw `ImDrawList` access. Non-finite or empty geometry is ignored.
- Painters run while the frame is still active (state, frame info, theme and `RequestRedraw` work), but node and portal declaration is closed during painting.
- Memory: `Node::kind` moved next to the other single-byte fields, so `Node` stays 432 bytes despite the extra `NodeStyle` pointer; the 2,001-node budget is unchanged at 1,040,720 bytes.
- New theme tokens `Theme::ink` (blended in theme transitions) and `Theme::ink_transition`.
- New `lumen_kit` target with `Kit::BeginInkSurface()`: the ink splash from handoff item 3, built on the public event, state, theme and painter APIs. (Superseded below: the ink now follows the look spec's grow-and-fade.) Only the innermost surface splashes, and `PreventDefault`, disabled surfaces and reduced motion suppress it.
- Tests: `TestCustomPainter` (`lumen_ui_tests`) covers paint order, opacity, ID/bounds, declaration lockout, clip balance, state access, and null/hidden painters. `lumen_kit_tests` covers spread geometry, hold, fade timing, the redraw demand settling, PreventDefault, disabled, reduced motion and nested surfaces. A mid-spread and a full-spread headless render were checked visually.
- Limits: the Canvas has no paths, gradients or per-corner radii yet (these follow plan 6.3). Core `Button` has no ink of its own; Kit buttons add it.

## Material theme and Kit buttons — implemented, partial against the look spec

- `ThemeKind::Material` with the Material 3 baseline (light) values from `docs/GPT_LOOK_INSTRUCTIONS.md`: colours, scrim 0.36, shadow 0.30, ink alpha 0.26, radius 20, title 24, 40 px buttons with 24 px padding, 0.15 s `CubicOut` colour/text transitions and a 0.55 s `QuadOut` ink transition. Existing roles are reused where they correspond (canvas = surface, surface = surface_container, surface_alt = surface_container_highest, text = on_surface, muted_text = on_surface_variant, border = outline, accent = primary, accent_text = on_primary); `Theme` gains `surface_container_high`, `outline_variant`, `primary_container`, `secondary_container`, `on_secondary_container`, `inverse_surface`, `inverse_on_surface` and `inverse_primary`, filled for every built-in theme and blended in theme transitions. High Contrast still disables shadows.
- Ink now follows the spec: it starts 4 x 4 at the press point, grows to the bounds over `Theme::ink_transition` and its alpha falls as `ink.a * (1 - t)`; it no longer holds while pressed. `Kit::WithInk()` attaches ink to any node style with an optional tint.
- `Kit::Button` with `Filled`/`Tonal`/`Outlined`/`Text` variants built on core `Button` (so keyboard focus, Enter/Space activation and click handlers are unchanged), `Kit::ButtonStyle()` presets and `Kit::StateLayer()`. Filled and Tonal lift on hover (blur 12, offset 4); Outlined and Text use primary at 8% hover and 6% pressed; Filled ink is `on_primary`, others `primary`.
- Gallery: every button is a Kit button, the theme toggle cycles Material, Dark and Light, Material is the default, and page cards are flat tonal fills (no border, no shadow); overlays keep their shadow.
- Tests in `lumen_kit_tests`: `TestInkSpreadAndFade`, `TestInkSuppressedAndNested`, `TestMaterialTheme`, `TestButtonStyles`, `TestButtonInteraction` (state-layer hover fill, variant ink tint, click activation). The sanitizer build passes. Headless renders of the gallery at rest, with a Filled button mid-splash, and with the toast and modal open were checked visually.
- Not yet done from the look spec: the 1 px press squish, a separate 0.10 s press transition (pressed uses the colour transition), radius/spacing scale tokens. (Motion tokens, the font ladder, Switch, Segmented, Toast and AlertDialog followed in the next section.) Disabled Kit buttons use the core disabled text colour (`muted_text`) rather than on_surface at 38%.

## Kit v0 widgets, font ladder and showcase gallery — implemented, tested

Engine:
- Font ladder (plan 2.7): `SetFontLadder(context, fonts, count)` registers up to 8 host-baked fonts; text snaps to the nearest rung and is measured and drawn at its baked size, including `Canvas::Text`. Type tokens `Theme::caption_size` and `heading_size` join `font_size`/`title_size`. Re-baking at framebuffer scale is not implemented.
- Enter/Space dispatches `Click` to any focused focusable node (previously only `Button`), so custom controls are keyboard-activatable.
- `PortalOptions::pin` (centre or any edge/corner; `pin_to_top_right` kept as an alias), `offset` applied to pinned and centred portals, and `size.y == 0` for content-height portals. `GetLayerVisibility()` and `IsLayerClosing()` expose layer Presence state.
- Theme tokens: `toggle_transition`, `enter_transition`, `exit_transition`, `reflow_transition` and `inverse_info/success/warning/danger`; Material uses the look-spec values (0.32 s BackOut, 0.55 s BackOut, 0.26 s CubicIn, 0.38 s cubic-bezier(0.4, 0, 0.2, 1)).
- Bug fix: a label laid out at exactly its measured width rendered its last word on a second line (ImGui word wrap at an equal width). Text that fits now draws unwrapped.
- Test harness fix: `HostFrame` in `test_ui` called `SetNextWindow*` before `ImGui::NewFrame()`, so the test window was a tiny default box that CPU-clipped all text; earlier tests only measured fills and did not notice.

Kit: `Switch`, `Segmented`, `CardStyle`, `AlertDialog` (with `OpenAlertDialog`) and the `ToastQueue` stack (`ShowToast`, `Toasts`). The toast stack uses one pinned portal; cards are absolutely positioned at animated slots, enter/exit offsets and fades follow the theme tokens, the badge pops with the enter curve (clamped to 1.18), and the countdown pauses while the card is hovered.

Gallery: rewritten as a Material showcase of the Kit and engine features; the X11 host bakes Roboto Medium at 12–30 px, uses a borderless full-window ImGui window and clears with the theme canvas.

Tests: `TestIntrinsicWidthLabelDoesNotWrap`, `TestKeyboardActivatesFocusableContainer`, `TestPortalPinsOffsetAndContentHeight`, `TestLayerVisibilityAndClosing`, `TestFontLadder` (`lumen_ui_tests`); `TestSwitchAndSegmented`, `TestAlertDialog`, `TestToastLifecycle` (`lumen_kit_tests`). Mutation checks (Button-only keyboard activation, a ladder that ignores size, a countdown that ignores hover) each fail the suite. ASan/UBSan clean. Headless renders of the gallery (Material at rest, with toasts, with the dialog, Dark theme, and the lower page) were inspected.

Partial / not done: switch thumb overshoot and toast motion were checked visually only by single frames, not as a video; toast enter/exit uses per-card opacity and offset, while the layer's own Presence fade (colour transition) also applies on first open and last close; the 1 px button press squish, framebuffer-scale font re-baking, ellipsis policy and non-Latin-1 glyph ranges (the gallery avoids em dashes for that reason) remain open.

## Text input, Canvas paths and icons — implemented, partial against plan 8.7 / 6.3 / 6.6

Engine:
- Keyboard events: `KeyDown` now auto-repeats (`UIEvent::repeat`; Tab traversal, Escape dismissal and Enter/Space activation still trigger only on the first press). New `UIEventType::TextInput` delivers each printable character from ImGui's input queue to the focused node. Key and text events carry `UIEvent::modifiers`.
- `GetClipboardText()`/`SetClipboardText()` (host clipboard via the ImGui bridge), public `MeasureText()` (font ladder aware, optional range end) and `GetCurrentNodeId()`.
- `Canvas` path builder: `BeginPath`, `MoveTo`, `LineTo`, `QuadTo`, `CubicTo`, `ArcTo`, `Stroke` (open/closed) and convex `Fill`. Non-finite points and degenerate paths are ignored and the path is always cleared.

Kit:
- `TextField`: single-line UTF-8 editing as described in the README; editing state (caret, anchor, scroll, blink) lives in the state store; drawing and hit-testing share `MeasureText`, so they agree with the font ladder.
- Icon set (`IconId`, `DrawIcon`, `Icon`) on Canvas paths; toast badges and close buttons use it, and toast heights now use `MeasureText` instead of calling ImGui directly. The Kit no longer calls ImGui functions itself.

Gallery: a Text input card (live greeting, read-only field), a search field that filters the 5,000-row virtual list, and an icon row.

Tests: `TestCanvasPathsMeasureAndCurrentId` (`lumen_ui_tests`); `TestTextFieldEditing` (`lumen_kit_tests`: typing, caret movement, word jumps, selection replacement, word delete, UTF-8 delete, capacity truncation, copy/cut/paste, auto-repeat, Space not activating, disabled). Mutation checks (no TextInput dispatch, no key repeat, no word movement) each fail the suite. ASan/UBSan clean. Headless renders of the filtered list, focused field with caret, icon row and icon toast badges were inspected.

Not done (plan 8.7): the editor is hand-written rather than built on `imstb_textedit.h`; no undo/redo, multiline, double-click word selection, IME candidate placement or inline composition; Canvas has no dashed strokes, per-corner radii, joins/caps control or concave fill (plan 6.3); icons are a small built-in set, not compiled Lucide/Phosphor data (plan 6.6).
