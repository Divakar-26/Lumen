# Lumen UI Engine

Lumen is a C++17 declarative UI engine built on Dear ImGui's public API. The host owns the window, event loop, renderer, fonts and ImGui context; Lumen supplies a bounded frame protocol, persistent state, node construction, basic layout, rendering commands, motion and input behavior.

The prior SDL3/OpenGL tween playground is not part of this project. The frozen implementation plan is in `Lumen_UI_Engine_Master_Plan_No_Section_Signs.md`.

## Build, test and run the gallery

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/lumen_gallery
```

The library and headless tests use the vendored Dear ImGui sources. On Linux, CMake enables `lumen_gallery` when X11 development files and an OpenGL library are available. The gallery is a sample host only; `lumen::core` does not depend on X11 or OpenGL. The gallery requires an X11 session. In a headless Linux environment, run it under Xvfb.

For a sanitizer build:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize --output-on-failure
```

To consume Lumen from another project, provide an `imgui::imgui` target and configure with `-DLUMEN_BUILD_BUNDLED_IMGUI=OFF`.

## Host integration

The host creates the ImGui context and window. Between `ImGui::NewFrame()` and `ImGui::Render()`, declare Lumen nodes inside the host's current ImGui window:

```cpp
Lumen::Context* ui = Lumen::CreateContext();

// Per frame, after ImGui::NewFrame() and inside a host-owned ImGui window:
const Lumen::Viewport viewport{
    Lumen::Rect{origin.x, origin.y, available.x, available.y},
    safe_area,
    framebuffer_scale
};
if (Lumen::BeginFrame(ui, viewport, delta_seconds)) {
    Lumen::NodeStyle panel{};
    panel.layout = Lumen::LayoutMode::Column;
    panel.gap = 12.0f;
    panel.padding = {16.0f, 16.0f, 16.0f, 16.0f};
    panel.has_background = true;
    panel.background = Lumen::GetTheme(ui).surface;
    Lumen::BeginNode(ui, "settings-panel", panel);
    Lumen::Label(ui, "title", "Settings");
    Lumen::Button(ui, "save", "Save", OnSave, app_state);
    Lumen::EndNode(ui);
    Lumen::EndFrame(ui);
}

// After the host calls ImGui::Render(), render its draw data as usual.
// Destroy the context before destroying the host's ImGui context.
```

Only `core/imgui_bridge.cpp` includes `imgui_internal.h`. Core rendering output is a frame-arena render-command list translated to `ImDrawList`; Lumen creates no windows, GPU objects or renderer resources.

## Current implementation

### Foundation and motion

- `lumen::core` static library, host-owned ImGui-context contract, frame protocol and redraw demand.
- Fixed-capacity frame arena and typed persistent state pool with stable IDs, pinning and expiry.
- Geometry primitives, real/manual clocks and input event injection.
- Easing families (including cubic Bézier and steps), typed tweens/springs, OKLab color interpolation, keyframes, timelines, controllers and per-context scheduling. Non-finite custom-curve results fall back to linear progress instead of contaminating animation state.
- Bounded `Router` navigation stack with copied route names, replace/push/pop semantics, explicit depth/overflow checks, bounded name scanning, and alias-safe replacement (`Replace(Current())`).
- Interruptible `Presence` enter/exit state with monotonic-time handling, opacity continuity on reversal, zero-duration transitions, and `NeedsFrame()` when unsettled. Non-finite custom easing results fall back to linear progress rather than contaminating opacity with NaN/Inf.
- New backend-independent `LayerStack`: bounded 32-entry z-order registry, paint-order copy, modal input-gating query, LIFO dismissal policies, monotonic timed expiry and `NeedsFrame()` for live timeout entries.
- Pure `ResolveAnchorPlacement()` helper with safe-area handling, side flipping, start/center/end alignment and final viewport clamping.
- Retargetable implicit animation state for layout and visual properties.

### Nodes, style, layout and painting

- Frame-scoped node tree with parent-scoped stable string/integer IDs and duplicate sibling ID rejection.
- Inherited text color, font size and opacity; Dark, Light, High Contrast and Material (Material 3 baseline light) themes. `Theme` carries the Material 3 colour roles (existing roles cover surface, surface container, on-surface, outline and primary; added roles cover the high container, outline variant, primary/secondary containers and inverse colours).
- Theme switching is interruptible: palette colors cross-fade in OKLab while size/spacing tokens interpolate linearly. `IsThemeTransitioning()` exposes status; reduce-motion finishes an active theme immediately and schedules repaint. Initial pre-frame theme setup is immediate.
- `BeginFrame` validates viewport bounds and display scale before mutating frame state; invalid dimensions/scales are rejected, and oversized safe-area insets are normalized to avoid inverted content geometry.
- Theme tokens for spacing, radii, button metrics, focus ring, wheel scrolling, transition timings/curves and layout springs.
- Column/row flex layout with grow/shrink, `flex_basis`, row wrapping, gap, alignment and justification; stack and absolute placement; uniform-column grid.
- Text wrapping and start/center/end alignment; rounded fills, borders, text, nested clipping and opacity through `ImDrawList`.
- Drop shadows / elevation (`NodeStyle::shadow`, a `ShadowStyle`): blur, spread, offset, themed colour (`Theme::shadow`) and pointer-target hover "lift". Pure `ImDrawList` (layered rounded rects, no shaders). Blur, offset and colour animate through the theme colour transition, so hover elevation is interruptible.
- Custom painters (`NodeStyle::custom_paint`, a `CustomPaint`): a `Painter` callback receives a `Canvas` with the node's ID, bounds and resolved opacity, opacity-aware fill/stroke rect, circle, line, text and clip primitives, and raw `ImDrawList` access. It paints after the node's fill and before its border, text and children, inside the node's clip. Painters can read state and request redraws but cannot declare nodes or portals; leftover clips are popped.
- Host-owned image resource seam: `Image()` accepts an `ImTextureID`, logical source dimensions, fit mode (`Stretch`, `Contain`, `Cover`) and a placeholder when no texture is supplied. Lumen never uploads or deletes GPU objects.
- Typed event routing through capture, target and bubble phases, with `StopPropagation()` and `PreventDefault()`; pointer capture keeps drag events routed after leaving the captured node.
- Pointer enter/leave/move/down/up/click, wheel and keyboard events; focus gained/lost; Tab/Shift+Tab focus traversal; Enter/Space activation; visible keyboard focus ring.
- Minimum hit targets and per-node `hit_slop`; nested scrolling chains unconsumed wheel delta to ancestor scrollers.
- `GetNodeRect()` reports computed node geometry from the last completed layout for integrations and tests.
- `Collection`: fixed-height vertical virtualization with stable index-keyed item wrappers, cache window and full logical scroll extent. The test suite exercises 100,000 logical items while building only the visible window.
- Linux X11/OpenGL gallery demonstrates animated theme switching, buttons, layout modes, token swatches, a virtualized 5,000-record feed, a pointer-capture drag lab, and actual anchored popover/modal/toast portals through the public Context API. The gallery uses a viewport-sized clipped scroller.

## Layers and integrated portals

See [`docs/LAYERS.md`](docs/LAYERS.md) for the Context-integrated portal API and lifecycle. `OpenLayer`/`CloseLayer` drive Presence transitions; `BeginPortal`/`EndPortal` declare detached overlay roots. Portals are laid out against the safe viewport, ordered and painted after the base tree, hit-tested before it, and support anchored placement or safe-area pinning. Modal portals draw a themed scrim, block background input, trap Tab focus and restore the prior focus target on close. Outside-click, Escape and timeout dismissal use the existing policies. The Linux gallery exercises an anchored popover, modal and timed toast using the same engine APIs.

The portal runtime is deliberately bounded (32 layers per context), nested portals and portal-to-portal anchors are rejected, and accessibility-grade focus scopes/multitouch remain future work. `lumen_kit` currently provides only the ink surface (below); toasts, dialogs, switches and material components are not implemented yet.

## Virtual collections

`Collection` takes the total item count and fixed item extent. Its callback is run only for visible rows plus the requested cache on either side. Declare each item's content under the callback's current row wrapper; any nested `BeginNode`/`EndNode` calls must balance.

```cpp
void BuildRecord(Lumen::Context* ui, std::size_t index, void*) {
    char label[64]{};
    std::snprintf(label, sizeof(label), "Record %06zu", index);
    Lumen::Label(ui, "record-label", label);
}

Lumen::NodeStyle list_style{};
list_style.width = 360.0f;
list_style.height = 280.0f;
list_style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
Lumen::Collection(ui, "records", 100000u, 28.0f, BuildRecord, nullptr, list_style, 3u);
```

Rows must use a fixed logical extent in this version. Variable-height rows, sticky headers, scroll indicators, animated `ScrollTo`, snapping, scroll physics/inertia and pull-to-refresh are not implemented yet.

## Event routing and pointer capture

Set `NodeStyle::event_handler` to receive typed events. Ancestors receive capture-phase events before the target; after the target, ancestors receive bubble-phase events in reverse order. `StopPropagation()` stops subsequent routing, while `PreventDefault()` suppresses the associated default action (for example click activation or wheel scrolling). A handler may capture the single ImGui mouse pointer during a `PointerDown` or active-drag `PointerMove`; captured `PointerMove` and `PointerUp` events continue to reach that node even outside its rectangle.

```cpp
void OnDrag(Lumen::Context* ui, Lumen::UIEvent& event, void*) {
    if (event.type == Lumen::UIEventType::PointerDown) {
        (void)Lumen::CapturePointer(ui, event.current_target_id, event.pointer_id);
    } else if (event.type == Lumen::UIEventType::PointerUp) {
        (void)Lumen::ReleasePointer(ui, event.current_target_id, event.pointer_id);
    }
}

Lumen::NodeStyle drag_style{};
drag_style.event_handler = OnDrag;
Lumen::BeginNode(ui, "drag-target", drag_style, "Drag me");
Lumen::EndNode(ui);
```

The `pointer_id` currently represents the single mouse pointer supplied by ImGui; multitouch is not yet implemented. `GetNodeRect()` returns geometry from the last completed layout and is useful for debugging or connecting Lumen to host-side tooling.

## What is not complete

This is a working early engine, not the finished master plan. The grid has uniform columns only (no track definitions, spans or named areas), and a custom `LayoutDelegate`, full constraint solving, responsive queries and variable-height virtualization are missing. Paint does not yet include gradients, per-corner radii, advanced text shaping/overflow policies or resource caches. JSON theme loading, context providers/signals, gesture arenas/touch gestures, drag-and-drop, rich focus scopes, text editing, shortcut dispatch, an accessibility tree, most Kit widgets and inspector/hot reload tools remain outstanding. `Presence` now drives portal enter/exit and timeout transitions; `Router` remains a standalone navigation primitive and is not yet connected to page/view lifecycle. The keyboard implementation is deliberately limited to focus traversal, event delivery and button activation. Platform validation is Linux-only so far; Windows/macOS and multiple ImGui versions remain unverified.

See `docs/MILESTONE_STATUS.md` for the concrete status and remaining milestone gates. The portal demo can be launched directly in a deterministic state with `./build/lumen_gallery --popover`, `--modal` or `--toast` (use one flag at a time).

## General interaction state

Pointer-enabled nodes can resolve hover and pressed selectors, including non-button containers. A hit on a descendant also marks pointer-enabled ancestors as hovered/pressed, so card surfaces can provide consistent interaction feedback while nested controls remain the hit targets. Containers with `has_hover_background`, `has_pressed_background`, or a hover-enabled `ShadowStyle` participate in hit testing in their empty areas. Hover shadow lift is not restricted to `Button`.

`GetNodeState(context, node_id)` returns `NodeInteractionState` (found, hovered, pressed, focused, focus-visible) for the last completed frame. Query it after `EndFrame` and before the next `BeginFrame`, because the node tree is frame-scoped. No per-node fields were added for this feature.

Build the headless interaction-state screenshots (no X11/GPU required):

```sh
cmake --build build --target lumen_interaction_screenshot --parallel
./build/lumen_interaction_screenshot interaction-hover.png hover
./build/lumen_interaction_screenshot interaction-pressed.png pressed
```

## Kit

`lumen_kit` (`lumen::kit`) is a separate static library built only from the public engine API (`kit/include/lumen/kit/`).

- `Kit::WithInk(context, style, tint)` adds an ink splash to any node style: a rounded shape starts 4 x 4 at the press point and grows to the node bounds over `Theme::ink_transition` while its alpha falls from `Theme::ink`'s alpha to zero. Only the innermost inked node under the pointer splashes; the style's own event handler and painter still run, and `PreventDefault()` on `PointerDown` suppresses the splash. Disabled nodes and reduced motion show no ink. `Kit::BeginInkSurface()` is a container with ink (close it with `Lumen::EndNode`).
- `Kit::Button(context, key, text, ButtonVariant, handler, user_data[, style])` with `Filled`, `Tonal`, `Outlined` and `Text` variants: pill shape at `Theme::button_min_height`, theme colours, 8% state-layer hover/pressed fills (`Kit::StateLayer`), a hover lift shadow on Filled and Tonal, and variant-tinted ink. `Kit::ButtonStyle()` returns the preset for adjustment. The gallery uses these buttons and starts in the Material theme.
# Lumen
