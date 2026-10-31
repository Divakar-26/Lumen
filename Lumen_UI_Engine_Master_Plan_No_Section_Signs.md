# Lumen UI Engine: Master Plan (Frozen)

Lumen is a general-purpose UI engine on raw Dear ImGui: ten primitives (node, style, layout, paint, motion, input, layers, state, resources, host) that you compose into any interface, from a settings screen to a node editor to a game HUD. Widgets and the Apple-style look are an optional Kit built only from those same public primitives, so no future need forces a redesign. "Lumen" is a working name: one CMake variable and one namespace.

## 0. Charter (immutable rules)

Section numbers are permanent IDs that code, commits and reviews cite ("implements 7.3"). These thirteen rules hold forever; each exists because breaking it would force a redesign.

### 0.1 Mechanism over policy.

The core decides how things are laid out, painted, animated and hit-tested. It never decides what a button looks like or does; that lives in the Kit and in themes, and can be replaced.

### 0.2 Dogfooding.

The Kit, tools and gallery use only public API. If they need something, it becomes public API, so anything they can build, you can build.

### 0.3 Dependencies.

The core depends only on C++17 and `imgui.h`. ImGui internals (`imgui_internal.h`) are touched in exactly one file, `core/imgui_bridge.cpp`.

### 0.4 The host owns the world.

The engine never owns a window, swapchain, main loop, thread or GPU resource. At Tier 0 it creates no textures, buffers or shaders; images arrive as host-provided `ImTextureID`s.

### 0.5 ImGui is the only I/O.

Drawing goes through `ImDrawList`, input through `ImGuiIO` (or is injected into it), text through ImGui fonts. No second renderer, input system or font system.

### 0.6 Everything animates.

Any number, color, vector, transform or style property, and any value you own, can be tweened, sprung or keyframed by one motion system with no per-widget special cases.

### 0.7 Nothing snaps; everything is interruptible.

A visible change animates, and can be redirected mid-flight without a jump in position or velocity.

### 0.8 No hard-coded look.

Colors, sizes, radii, durations, curves and springs come from style and theme tokens, never from engine code.

### 0.9 Determinism.

The same input trace, clock, theme and viewport produce a byte-identical render list. Time comes from an injectable clock, and the only global is one `Context*`.

### 0.10 Zero steady-state allocation, one UI thread.

Per-frame memory is an arena. Other threads talk to the UI thread through queues.

### 0.11 Tiered capability.

Every feature has a Tier 0 implementation on pure `ImDrawList`. Tier 1 (blur, shaders, render-to-texture) is optional and falls back silently.

### 0.12 Licensing.

Only MIT, BSD, ISC, OFL and Apache assets ship. Apple's fonts and SF Symbols are never bundled or copied.

### 0.13 Non-goals.

Not an HTML/CSS renderer, not a scripting VM, not a 3D scene renderer, not a windowing layer, not a visual designer.

## 1. Model: ten primitives

Everything in Lumen is one of ten primitives, and everything else, including the Kit, is built from them. They share one per-frame pipeline: declare nodes, resolve style, lay out, paint, emit.

*Embedded content: architecture — app and Kit on nine core primitives, one bridge, one host.*

Nine primitives live in the core; the tenth, the host contract, is the boundary at the bottom. Only the bridge touches ImGui, and the engine owns no window, loop or GPU resource.

| Primitive | What it guarantees | Section |
| --- | --- | --- |
| Node | One tree element type: identity, style, content, children, behaviors, layout mode | 3 |
| Style | Flat properties, states, variants, tokens and transitions | 4 |
| Layout | Pluggable modes sharing one sizing model, plus scrolling and virtual collections | 5 |
| Paint | A backend-free render list translated to `ImDrawList`: shapes, effects, text, custom painters | 6 |
| Motion | One system for tweens, springs, keyframes, timelines, presence and physics | 7 |
| Input | Events, hit testing, gestures, focus, keyboard, drag and drop, accessibility | 8 |
| Layers | Overlays, portals, anchoring and modal scopes | 9 |
| State | Keyed persistent state, inherited context values and signals | 3.4 to 3.8 |
| Resources | Fonts, images, icons and themes through seams; no GPU objects owned | 2.7, 2.8, 6.6 |
| Host | Frame protocol, input injection, redraw requests, DPI and coexistence with ImGui | 2 |

On top of the primitives sit the optional Kit (10) and the tools (13). Behaviors (10.1) are small abilities attached to nodes; they use Input, Motion and State and are not an eleventh primitive.

## 2. Host contract (integration)

The host calls two functions per frame and supplies a few hooks. The window, graphics API and main loop stay yours.

### 2.1 Frame protocol.

```cpp
Lumen::Context* ui = Lumen::CreateContext(cfg);        // once

// every frame, between ImGui::NewFrame() and ImGui::Render()
Lumen::BeginFrame(ui, viewport, dt);   // viewport: logical rect, safe-area insets, scale
    BuildUI();                         // your declarations (3)
Lumen::EndFrame(ui);                   // style, layout, paint, emits ImDrawList commands
```

A Lumen tree can fill the whole viewport or live inside one ImGui window; several contexts can coexist.

### 2.2 Backends.

Any ImGui backend works (SDL2/3, GLFW, Win32, OpenGL, Vulkan, DirectX, Metal, WebGPU, Android). The engine never calls a graphics or windowing API. `hosts/` ships reference adapters for SDL3 + OpenGL3 (your boilerplate), GLFW + Vulkan and Win32 + DX11; each is about a hundred lines and only demonstrates this protocol.

### 2.3 Input.

Input is read from `ImGuiIO`. For embedding (in-game panels, UI on a 3D surface, recorded or remote input) the engine offers `Inject*` helpers that call the matching `io.Add*Event` functions on the context's ImGui context. Touch is recognized through ImGui's `ImGuiMouseSource_TouchScreen`.

### 2.4 Redraw and idle.

`NeedsFrame()` is true only while an animation runs, input arrived or a signal fired. An idle app can block on the host's event wait and present nothing new.

### 2.5 DPI and scale.

Layout units are logical pixels. The host passes the framebuffer scale; paint snaps strokes and text to physical pixels at rest. A scale change triggers the font hook in 2.7.

### 2.6 ImGui compatibility.

Supported: the bundled snapshot (`IMGUI_VERSION_NUM` 19193, labeled 1.92 WIP: baked font atlas, no docking) and later releases. The bridge hides the differences, CI builds both, and nothing outside the bridge checks a version. Docking and multi-viewport are never required or used.

### 2.7 Fonts.

The bridge defines a `FontProvider` with two implementations chosen at compile time.

- **Classic** (the bundled ImGui): fonts are baked per size. The engine bakes a size ladder per family and weight from the type tokens, snaps text to the nearest rung, and asks the host to rebuild the font texture when the scale changes, through a `RebuildFontTexture` hook between frames (for OpenGL3 that is `ImGui_ImplOpenGL3_DestroyFontsTexture` then `CreateFontsTexture`; each host adapter wires its own).
- **Dynamic** (ImGui releases with runtime font sizing): any size on demand, no ladder, no rebuild hook.

Animated text scales by transform, never by changing font size, so animation never churns the atlas.

### 2.8 Images and GPU resources.

The `ImageProvider` seam lets the host decode and upload textures and hand back an `ImTextureID`, size and UVs. The engine handles fit modes, rounding, fade-in and placeholders. It never creates a GPU object (0.4).

### 2.9 Threading.

All engine calls happen on one thread. Assets loaded elsewhere arrive through a host-owned queue that the providers drain at `BeginFrame`.

### 2.10 Coexistence.

`Lumen::Host(id, rect, fn)` draws a Lumen tree inside any ImGui window, and `ImGuiEmbed` places raw ImGui widgets inside a Lumen node. An existing ImGui app adopts Lumen one panel at a time.

## 3. Nodes, identity and state

The UI is a tree of nodes declared into an arena every frame. All persistent state lives outside the tree under stable IDs.

### 3.1 One node type.

A node has an ID, a parent, a style, optional content (text, image or a custom painter), children, behaviors (10.1) and a layout mode (5). There are no widget subclasses: a button is a node plus a style class plus the pressable behavior.

### 3.2 Per-frame flow.

Declare, resolve style, lay out, paint, emit. Input is processed against the previous frame's layout, so hit testing needs no second declaration pass. For moving content the engine hit-tests the animated position the user last saw.

### 3.3 Identity.

A node's ID is a 64-bit hash of its parent's ID and its key. The key is, in order: an explicit string or integer; else the call site (`LUMEN_ID`); else the child index. Dynamic lists must supply explicit keys, and debug builds warn on missing or duplicate ones, because index-based IDs make animation state jump when items reorder. Roots derive from ImGui's ID stack, so engine trees compose with raw ImGui.

### 3.4 State store.

An open-addressing map from ID to trivially destructible blobs, with typed `State<T>` accessors, holds animation values, scroll offsets, focus, selection, caret, expanded flags and anything you own. Entries unseen for N frames (default 120) are evicted unless pinned. Memory is pooled, so there is no per-frame allocation.

### 3.5 Context values.

Values inherited down the tree without threading parameters: theme, text direction, locale, density, reduce-motion, safe area, accent. `Provide(key, value)` and `Use(key)`; a change re-resolves only the affected subtree.

### 3.6 Presence (exit animations).

An immediate-mode UI forgets a node the moment you stop declaring it, which would make fade-outs impossible. A `Presence` node fixes this: when its child subtree disappears, the engine keeps a frozen copy of the subtree's last resolved nodes in a pooled buffer, keeps painting it while the exit transition runs, then frees it. Enter and exit use the motion system (7). The cost is one subtree copy at exit, so Presence is opt-in per subtree.

### 3.7 Three ways to build a tree.

All three produce the same nodes and can be mixed.

1. Scoped calls: `Begin(node)` / `End()`. The lowest level, nothing hidden.
2. Lambda builders: `Row({...}, [&]{ ... })`. Convenient composition with designated-initializer options.
3. Data-driven: a JSON layout file loaded by `extras/layout_json`, with data bindings and hot reload. No code execution.

### 3.8 Signals (optional).

`Signal<T>` requests a redraw when its value changes, so an otherwise idle app wakes. The engine imposes no application architecture: bind pointers, use signals, or pass values every frame.

## 4. Style, state and theme

Style is a flat set of properties, themes are token trees, and every property change can transition. That combination gives Apple-like polish without any widget doing animation work.

### 4.1 Properties.

- Layout: display mode, direction, width and height, min and max, aspect ratio, grow, shrink, basis, padding, margin, gap, align, justify, position, insets, z-index, overflow.
- Box: fill or gradient, border width, color and style, per-corner radius with smoothing, a list of shadows, opacity, backdrop material.
- Text: family, weight, size token, color, alignment, line height, tracking, decoration, overflow, max lines.
- Transform: translate, scale, rotate, origin.
- Interaction: cursor, hit slop, pointer-events.

### 4.2 State selectors.

A style can override properties per state: `hovered`, `pressed`, `focused`, `focus-visible`, `disabled`, `selected`, `checked`, `dragging`, `drop-target`, `loading`, plus app-defined states. Behaviors compute states; styles never hard-code them.

### 4.3 Resolution order.

Engine defaults, then the theme's component style, then classes and variants in the order given (`primary`, `danger`, `compact`), then the inline style. State overrides apply last at each level. Resolution is deterministic and traceable in the inspector.

### 4.4 Transitions.

Any property can declare a transition (duration and curve, or a spring). When its resolved value changes, the engine animates from the value currently displayed, not from the old target, so retargeting mid-flight is continuous. This is the mechanism behind hover, press, focus and theme changes. The built-in themes give every visible property a transition; opting out is explicit.

### 4.5 Tokens.

A theme is a token tree: semantic colors, type scale, spacing scale on a 4 pt grid, radii, elevations, motion (durations, curves and springs per interaction class), materials, control sizes, breakpoints and icon set. Components read tokens only (0.8).

### 4.6 Semantic color and appearance.

Colors are semantic (label, secondary label, fill, separator, grouped background, tint, destructive and so on), each with light, dark and high-contrast values. Switching appearance cross-fades every color through the transition system. Interpolation defaults to OKLab so midpoints do not turn muddy.

### 4.7 Scoped themes.

`ThemeScope` overrides tokens for a subtree and nests freely.

### 4.8 The Apple look is a theme, not code.

Built-in themes are `iOS`, `macOS`, `Neutral` and `HighContrast`. A theme sets metrics (touch targets at least 44 pt, desktop controls smaller), corner smoothing, materials, springs, type scale and scrollbar and separator idioms. Changing platform idiom changes the theme, not the components.

### 4.9 Data-driven themes.

Themes load from and save to JSON, hot reload in development builds, and switch at runtime with animation.

## 5. Layout

Layout is pluggable: built-in modes cover most interfaces and a custom delegate covers the rest, all sharing one sizing model.

### 5.1 Sizing model.

Each node receives min and max constraints from its parent, reports a size, and is positioned by its parent. Sizes can be fixed, fractional, content-fit or fill. One pass resolves most subtrees in O(n); intrinsic measurement (sizing a parent from its children's natural sizes) is opt-in because it costs a second pass.

### 5.2 Modes

(the `display` style property):

- **Flex:** row or column, wrap, grow, shrink, basis, gap, align and justify, with CSS flexbox semantics so existing knowledge transfers.
- **Grid:** fixed, fractional, auto and min/max tracks, spans, named areas, auto-placement.
- **Stack:** children overlap, aligned or anchored to edges and to other nodes' edges.
- **Absolute:** explicit insets and size, relative to the nearest positioned ancestor.
- **Flow:** inline wrapping of text spans and inline nodes.
- **Custom:** a `LayoutDelegate` receives constraints and the children's measure functions and returns positions (radial, masonry, timeline, node graph).

### 5.3 Scrolling.

Any node can scroll on either axis: clipping, offset in the state store, physics (8.6), indicators as a theme-controlled overlay, animated `ScrollTo`, snapping, sticky children, nested scroll chaining, a pull-to-refresh hook, and anchor preservation when content above the viewport changes size.

### 5.4 Virtual collections.

A `Collection` node builds only the visible items plus a cache extent from an item count and a builder. It serves lists, grids, tables and trees of any size, with variable item heights (measured on first sight, cached by key) and sticky headers. Anything that can exceed a few hundred nodes must use it.

### 5.5 Responsive.

Constraints are always available to the subtree, so a node can branch on its own width: `ResponsiveValue<T>{compact, medium, expanded, large}` and a `LayoutQuery` callback. Default breakpoints (600, 840 and 1200 logical px) are theme tokens. Density, text scale, orientation, safe area and on-screen keyboard insets are context values (3.5).

### 5.6 Animated layout.

Any change to a node's laid-out position or size animates from its previous rectangle (the FLIP technique): inserted, removed, reordered and resized items glide into place. Shared-element transitions match nodes across routes by `SharedKey`. Opt-out is per node.

### 5.7 Text layout.

Measurement goes through ImGui fonts with a per-frame cache. Supported: wrapping, ellipsis, max lines, alignment, rich spans with per-span style, tabular numerals, tracking, line height and selectable text. Complex scripts and bidirectional text go through the `TextShaper` seam (15); RTL mirroring of layout works without it.

### 5.8 Pixel rules.

Layout runs in float logical pixels. Paint snaps strokes and text origins to physical pixels at rest, and does not snap during animation, so motion stays smooth.

## 6. Paint

Paint produces a backend-free render list of simple commands, and one translator turns that list into `ImDrawList` calls. Clipping, transforms and opacity groups are resolved there, once, instead of in every component.

### 6.1 Render list.

Plain-data commands in an arena: fill and stroke shapes, shadows, text runs, images, paths, gradients, push and pop of clip, transform, opacity and layer, and a callback command. It exists so that (a) transforms, group opacity and rounded clipping are solved in one place, (b) static subtrees can be cached and replayed (`RepaintBoundary`), (c) goldens and tools can diff it, and (d) Tier 1 effects slot in as layer boundaries.

### 6.2 What `ImDrawList` can and cannot do.

The bundled ImGui draws filled and stroked shapes, per-vertex colors, rectangular clips, textured quads and raw triangle meshes (`PrimReserve`, `PrimWriteVtx`). It has no blur, no rounded or arbitrary clip, no group opacity and no transforms. Lumen's Tier 0 answer for each:

| Need | Tier 0 approach (pure ImDrawList) | Limit |
| --- | --- | --- |
| Continuous (squircle) corners | Cached path templates per radius and smoothing, convex fill | Cost grows with segment count; capped per corner |
| Soft shadows | Ring mesh with per-vertex alpha following a Gaussian profile; no texture | About 300 to 500 vertices per shadow; optional baked 9-slice path for heavy scenes |
| Gradients | Vertex-color meshes: linear, radial (fan), conic (wedges) | 8-bit banding on very long smooth ramps |
| Transforms on a subtree | Transform the emitted vertex range | Clip rectangles stay axis-aligned |
| Group opacity | Multiply vertex alpha over the subtree's range | Overlapping children show through; exact groups need Tier 1 |
| Rounded clip of any content | CPU-clip triangles against a convex rounded polygon, interpolating color and UV | Cost grows with vertices in the subtree; images use `AddImageRounded` |
| Backdrop blur and vibrancy | Tier 0: translucent tint, gradient and hairline. Tier 1: `BackdropProvider` captures and blurs via an `ImDrawCallback` | Tier 1 is per-graphics-API code in `backends/`, optional |
| Text | ImGui fonts (2.7) | No shaping or bidi without the seam; rotated text is vertex-transformed and slightly soft |
| Icons | Vector path data drawn as paths | None |

### 6.3 Shapes and strokes.

Rectangles with per-corner radii and smoothing from 0 to 1, circles, capsules, lines, polylines with joins and caps, arbitrary paths (line, quadratic, cubic, arc), dashed strokes, inner, center and outer borders, pixel-snapped hairlines, antialiased fills.

### 6.4 Color.

sRGB storage, OKLab interpolation, HSL and OKLCH helpers, alpha handled once at translation.

### 6.5 Typography.

Type-scale tokens (size, weight, tracking, line height) named like a common hierarchy from large title down to caption. The default family is a bundled OFL humanist sans (Inter), so no proprietary font is needed. The family is a token, so any TTF or OTF drops in. Optional FreeType (in the bundle's `misc/freetype`) gives hinting control and variable weights.

### 6.6 Icons and images.

Icons are vector path sets from permissively licensed libraries (Lucide, Phosphor) compiled to compact data; size and stroke weight animate. Images support fit modes, rounded clip, placeholder, fade-in on ready and nine-slice. Both arrive through seams (15).

### 6.7 Materials.

Thickness presets (ultrathin, thin, regular, thick, chrome) for bars, sheets and sidebars. The API is identical at both tiers; with no `BackdropProvider` registered the engine uses Tier 0.

### 6.8 Polish rules.

Hairlines at one physical pixel. Strokes snapped at rest. Press states combine scale (about 0.97) with a tint change. Focus rings animate in. No hard color flashes. Shadows follow elevation tokens as an ambient layer plus a key layer.

### 6.9 Custom paint.

Any node can have a `Painter` callback that receives a `Canvas`: the same primitives as above plus raw `ImDrawList` access. Charts, graph canvases and game widgets are painters.

## 7. Motion

One motion system serves everything: values you own, style properties, layout and gestures. Springs are the default; tweens and keyframes cover the rest.

### 7.1 Core types.

`Value<T>` (current, target, velocity), `Tween<T>`, `Spring<T>`, `Keyframes<T>`, `Timeline` and `Controller`. `T` is any type with a `Lerp` trait: float, Vec2, Vec3, Vec4, color (in OKLab), rectangle, insets, transform, or your own.

### 7.2 Implicit animation.

`T cur = Animate(id, target, spec)` returns the current value and retargets by itself whenever `target` changes. This one call is what makes a UI feel alive with almost no code.

### 7.3 Explicit control.

A `Controller` supports play, reverse, pause, seek, repeat, ping-pong, speed, delay and callbacks (started, updated, completed, cancelled). Controllers live in the state store and survive frames.

### 7.4 Choreography.

`Sequence`, `Parallel`, `Stagger(delayPerItem, from)`, labels, relative offsets, seek and scrub, including scroll-linked scrubbing.

### 7.5 Curves.

Linear; Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic and Bounce, each In, Out and InOut; `CubicBezier(x1, y1, x2, y2)`; `Steps(n)`; `Custom(fn)`. Named presets: Standard (0.25, 0.1, 0.25, 1), EaseIn, EaseOut, Emphasized, Decelerate, Accelerate.

### 7.6 Springs.

A closed-form damped spring (under-, critically and over-damped) is exact at any frame rate and any dt spike. Retargeting carries position and velocity. A spring is specified by `duration` and `bounce` (presets Smooth 0, Snappy 0.15, Bouncy 0.3, plus Interactive and Gentle), by `response` and `dampingRatio`, or by raw `mass`, `stiffness` and `damping`. For bounce at or above 0: stiffness = (2π / duration)², damping = 4π (1 − bounce) / duration. A spring is at rest only when displacement and velocity are both under epsilon, and then it costs nothing.

### 7.7 Physics helpers.

Exponential-decay momentum with normal and fast deceleration rates, rubber-banding `(1 − 1 / (x·c/d + 1))·d` with c about 0.55, `ProjectedEnd(position, velocity)` for snap targets, a detent and snapping solver, friction, magnet-to-target, and velocity trackers for gestures.

### 7.8 Gesture-driven motion.

A gesture writes position directly while active, then hands its release velocity to a spring. No animation ever restarts from zero when interrupted.

### 7.9 One system underneath.

Style transitions (4.4), Presence enter and exit (3.6), animated layout (5.6), route transitions (10.3) and shared elements are all configurations of this system.

### 7.10 Scheduler and clock.

All motion reads `Clock` (real, manual or scaled). The scheduler advances active animations once per frame with a clamped dt, reports `NeedsFrame()` while any is active (2.4), and supports a global time scale for debugging.

### 7.11 Reduce motion.

One global flag converts springs and translations to short fades, removes parallax and clamps durations. Every built-in honors it.

### 7.12 Use outside UI.

`Animate` works on any float in your own code, so a camera, a progress meter or a counter in your game or app uses the same engine.

## 8. Input and interaction

Input is typed events routed through the node tree, with gestures, focus and accessibility built on the same events.

### 8.1 Events.

Pointer (mouse, touch and pen through ImGui's mouse source), wheel and trackpad, keyboard, text input (including IME as the ImGui backend provides it), gamepad navigation and host-injected events. Events have capture and bubble phases and `stopPropagation`.

### 8.2 Hit testing.

Runs against the previous frame's layout and honors clip, transform, z-order and layers. Per-node hit slop (touch targets of at least 44 pt come from the theme). Hit shapes: rectangle, rounded rectangle, circle, path or a custom function. `pointer-events` can be none or auto.

### 8.3 Pointer capture.

A behavior can capture the pointer so a drag continues outside its node until release.

### 8.4 Gestures.

Recognizers for tap, double tap, long press, pan, swipe, pinch, rotate and hover intent, plus custom ones. A gesture arena resolves competition (scroll versus drag versus swipe-to-act) by slop thresholds and priority; custom recognizers join the same arena.

### 8.5 Focus.

A focus tree with scopes, tab order, directional navigation (arrow keys, gamepad), focus trapping for modal scopes, `focus-visible` semantics (rings for keyboard, not for mouse), programmatic focus and focus restoration.

### 8.6 Scroll physics.

Platform-flavored profiles as theme tokens: touch with momentum, rubber banding and snapping; desktop with wheel smoothing and overlay scrollbars. Both use the helpers in 7.7.

### 8.7 Keyboard and text editing.

A shortcut registry with scopes, chords, conflict detection and Cmd/Ctrl mapping. Text input is a behavior built on the stb text-edit core that ships with ImGui (`imstb_textedit.h`), giving selection, word movement, undo and redo, while the engine draws the caret, selection, placeholder and multiline scrolling itself. IME candidate windows are placed through ImGui's platform hook; inline composition is shown where the backend supplies it.

### 8.8 Drag and drop.

Typed sources and targets, a ghost in the drag layer (9), edge auto-scroll, drop indicators and an animated settle into the slot. OS file drops are forwarded by the host.

### 8.9 Selection.

Single, multiple and range selection with keyboard extension and marquee selection. Text selection can span nodes inside a selection scope.

### 8.10 Accessibility.

Every interactive node emits semantics (role, name, value, state, bounds, relations) into an accessibility tree each frame. The core exposes the tree; the `extras/accesskit` bridge publishes it to OS screen readers. Focus order, contrast, target size and reduce-motion are checked by the audit in 14.8.

### 8.11 Haptics and sound.

Callback hooks only (`HapticSink`, `SoundSink`). The host decides what they do.

## 9. Layers and overlays

Anything that must escape its parent's clip and stacking, such as a popover, a modal or a dragged item, goes through one layer system.

### 9.1 Layer stack.

Ordered layers: base, floating, popover, modal, toast, drag and debug. Each has its own root and is painted above the one before it. `Portal(layer, fn)` declares content into a layer from anywhere in the tree, keeping the identity, context values and theme of the declaring site.

### 9.2 Anchoring.

An overlay can anchor to a node with placement (above, below, leading, trailing, auto-flip), alignment, offset, collision handling against the viewport and safe area, and an optional arrow. It keeps tracking the anchor while the anchor animates or scrolls.

### 9.3 Modal scopes.

A modal layer traps focus, blocks input below with an animated scrim, dismisses on outside click, Escape or gesture, and restores focus on close.

### 9.4 Dismissal.

Layers dismiss in last-in-first-out order. Each overlay declares its policy: outside click, Escape, focus loss or timeout.

### 9.5 Mechanisms, not looks.

Tooltip hover intent, toast queues with timeout and swipe-to-dismiss, and submenu hover paths live here. Their appearance is a Kit style.

## 10. Behaviors and the Kit

A small, fixed set of behaviors gives nodes their abilities. The Kit is the open-ended catalog of components made from nodes, styles and behaviors, which is how the engine proves that it can build anything.

### 10.1 Behaviors (closed set).

Pressable (hover, press, click, long press, repeat), Focusable, Draggable, Droppable, Scrollable, Resizable (edges and corners), Reorderable, Selectable, Editable (text), Toggleable, ValueControl (drag along an axis, for sliders, dials and steppers), Pageable (snap pager), Collapsible, Virtualized (5.4), Anchorable (9.2) and Pannable and Zoomable (for canvases). A behavior computes states (4.2) and emits events. It draws nothing.

### 10.2 Viewport.

A node with a pan and zoom camera, transform-aware hit testing, culling and custom painters. It is the base for node editors, graphs, timelines, maps and design tools.

### 10.3 Routing.

A `Router` holds a stack of routes with per-route state preserved, transitions built on 7 and 5.6, interactive pop gestures, route IDs for deep links, and tab and split containers. A route is just a subtree with an ID.

### 10.4 Kit catalog (open-ended).

The Kit ships at least the categories below. Any component that meets the Definition of Done may be added at any time without changing this plan.

| Category | Components |
| --- | --- |
| Actions | Button variants, icon button, toggle switch, checkbox, radio, segmented control, slider and range slider, stepper, menu button, link, chips |
| Text input | Single-line, search, secure, numeric, multi-line, token field, validation states |
| Display | Text, label, icon, image, avatar, badge, card, divider, progress (bar, ring, indeterminate), skeleton shimmer, empty state, tooltip, sparkline and basic charts |
| Pickers | Menu, wheel, date and time, color, file-path hooks |
| Collections | Plain and inset-grouped lists, grid, sortable and resizable table, tree, accordion, tabs, pager, carousel, reorderable list |
| Navigation | Navigation stack with large-title collapse, tab bar, sidebar, toolbar, menu bar, breadcrumbs, split view, command palette, dock (panels, tabs, splits, drag to rearrange) |
| Overlays | Sheet with detents, popover, context menu, dropdown, alert, action sheet, toast, banner, HUD, drawer, lightbox |
| Desktop | Custom window chrome, splitters, shortcut display, marquee multi-select, resizable panels |
| Mobile | Swipe actions, pull to refresh, keyboard avoidance |

### 10.5 Definition of Done (every Kit component).

Built from public API only (0.2). Reads tokens only (0.8). Animated and interruptible (0.7). Operable by mouse, keyboard, touch and gamepad. Emits accessibility semantics. Safe for RTL and reduce motion. Present in the gallery in every state and theme. Covered by a unit test and a golden test.

### 10.6 Replaceability.

Any Kit component can be restyled through tokens, classes, variants, painters and state overrides, or replaced by re-implementing it from the same primitives, without forking the engine.

## 11. Coverage matrix

Every capability a complete UI engine needs has exactly one home in the table below. "Engine" means the core primitives, "Kit" the optional component layer, "Seam" something the host plugs in.

| Capability | Lives in | Section |
| --- | --- | --- |
| Immediate-mode authoring with persistent state | Engine | 3 |
| Flex, grid, stack, absolute, flow and custom layout | Engine | 5.2 |
| Scrolling, virtualization, sticky and snapping | Engine | 5.3, 5.4 |
| Responsive breakpoints, container queries, DPI, safe areas | Engine | 5.5 |
| Animated layout and shared-element transitions | Engine | 5.6 |
| Style system with states, variants and transitions | Engine | 4 |
| Themes, light, dark and high contrast, runtime switch | Engine and Kit | 4.5 to 4.9 |
| Tweens, springs, keyframes, timelines, stagger, physics | Engine | 7 |
| Enter and exit animations | Engine | 3.6 |
| Shapes, squircles, gradients, shadows, clipping, transforms | Engine | 6 |
| Blur and vibrancy materials | Engine (Tier 0), Seam (Tier 1) | 6.2, 6.7 |
| Rich text, wrapping, selection, editing, IME | Engine | 5.7, 8.7 |
| Complex-script shaping and bidirectional text | Seam, `extras/` | 15 |
| Icons and images | Engine and Seam | 6.6 |
| Mouse, touch, pen, keyboard, gamepad | Engine | 8.1 |
| Gesture recognition and arbitration | Engine | 8.4 |
| Focus and keyboard navigation | Engine | 8.5 |
| Drag and drop, reorder, selection, marquee | Engine | 8.8, 8.9 |
| Shortcuts and command palette | Engine and Kit | 8.7, 10.4 |
| Overlays, modals, popovers, tooltips, toasts | Engine and Kit | 9 |
| Navigation, routes and transitions | Engine and Kit | 10.3 |
| Infinite pan and zoom canvas | Engine | 10.2 |
| Docking and panels | Kit | 10.4 |
| Accessibility tree and screen-reader bridge | Engine, `extras/` | 8.10 |
| Localization, RTL, number and date formatting | Engine and Seam | 5.7, 15 |
| Haptics and sound feedback | Seam | 8.11 |
| Data binding, signals, inherited context | Engine | 3.5, 3.8 |
| UI from data files with hot reload | `extras/` | 3.7 |
| Embedding: multiple contexts, injected input, UI on textures | Engine and host | 2 |
| Any window or graphics backend | Host | 2.2 |
| Inspector, layout and animation debuggers, theme editor | Tools | 13.3 |
| Record, replay and golden testing | Tools and tests | 13.3, 14 |
| Profiling counters | Tools | 12.6 |
| C API for other languages | `extras/` | 13.6 |
| Clipboard, cursors, IME placement, file drops | Host, through ImGui hooks | 2, 8 |

## 12. Performance and frame pacing

### 12.1 Budgets

(reference: desktop, 1080p, mid-range GPU). For a 2,000-node tree: layout at most 0.5 ms and paint plus translation at most 1.0 ms of CPU. Steady state allocates nothing. ImGui batching is preserved, so draw calls change only at clip, texture or callback boundaries. A 100,000-item virtual collection scrolls at the display's refresh rate. CI enforces these (14.5).

### 12.2 Event-driven rendering.

`NeedsFrame()` (2.4) is false when no animation runs and nothing changed, so a settled UI costs zero CPU and GPU and the host sleeps until input or a timer.

### 12.3 Caching.

Text measurement; squircle, path and shadow-profile templates; layout subtrees keyed by input hash and constraints so unchanged subtrees skip layout; and render-list caching for `RepaintBoundary` subtrees, replayed with a new transform and opacity instead of regenerated.

### 12.4 Virtualization.

Collections (5.4) are required for large data. Debug builds warn when a non-virtual container exceeds a node threshold.

### 12.5 Culling.

Nodes outside the viewport and the active clip are not painted. Off-screen animations keep advancing only if they affect layout.

### 12.6 Profiling.

Per-phase timers and counters (nodes, layout passes, cache hit rates, commands, vertices, draw calls, state entries, allocations) are exposed to the inspector and to CI.

## 13. Repository, build, tools and versioning

```text
lumen/
  core/       the ten primitives; depends on imgui.h and the standard library only
  kit/        components and themes, built from public API only
  assets/     OFL fonts, icon path data
  backends/   optional Tier 1 providers (BackdropProvider)
  extras/     HarfBuzz shaper, AccessKit bridge, JSON layout, C API
  tools/      inspector, debuggers, theme editor
  hosts/      reference adapters (SDL3+GL3, GLFW+Vulkan, Win32+DX11)
  gallery/    reference app and visual regression suite
  tests/      unit, golden, determinism, benchmarks
  docs/       this plan, guides, API reference
```

### 13.1 Build.

C++17 and CMake 3.16 or newer, matching your boilerplate. Targets `lumen::core`, `lumen::kit` and `lumen::tools`. Lumen links to an `imgui` target that the host provides: your vendored copy, FetchContent, vcpkg or Conan.

### 13.2 Integration modes.

`add_subdirectory(lumen)`; an installed package through `find_package(Lumen)`; or an amalgamated `lumen.h` and `lumen.cpp` pair.

### 13.3 Tools.

Inspector (tree, constraints, style-resolution trace, state store, element picking). Layout debugger (bounds, padding, overflow). Animation debugger (time scale 0.1x to 2x, pause, step, curve and spring plots). Theme live editor. Record and replay of input and clock. Profiler overlay.

### 13.4 Versioning.

Semantic versions, a `LUMEN_API_VERSION` macro, at least two minor versions of deprecation notice, and an ImGui version range pinned in CMake and CI.

### 13.5 Conventions.

Namespace `Lumen`, PascalCase API, no exceptions, no RTTI, no macros in the public API except configuration and `LUMEN_ID`, one `Context*` with several contexts allowed, designated-initializer option structs, lambdas for children.

### 13.6 C API.

`extras/c_api` is a flat C ABI over the core for use from Rust, C#, Python, Zig and other languages.

## 14. Quality gates

### 14.1 Unit tests.

Headless, on an ImGui context with no renderer and a `ManualClock`: layout algebra, curves and springs (including retarget continuity and frame-rate independence at 30, 60 and 144 Hz), the state store, the gesture arena and style resolution.

### 14.2 Golden tests.

Hash and diff the render list for every component, state, theme and scale. An optional software rasterizer in `tests/` turns mismatches into diff images.

### 14.3 Determinism.

The same input trace and clock produce an identical render list on one platform, and within a documented float tolerance across platforms.

### 14.4 Fuzzing.

Layout trees, style resolution and text input under ASan and UBSan.

### 14.5 Benchmarks.

The 12.1 budgets run in CI. A regression of more than 10% fails the build.

### 14.6 CI matrix.

Windows (MSVC), Linux (GCC and Clang) and macOS (Clang), plus build-only Android and Emscripten jobs, each against both ImGui profiles (2.6).

### 14.7 Static checks.

clang-tidy, warnings as errors, and a license audit of every shipped asset (0.12).

### 14.8 Accessibility audit.

The gallery checks contrast, focus order, target size and reduce-motion behavior.

## 15. Seams

The set of seams is fixed, and a seam's interface may only grow additively. New capability arrives through one of these seams or as a new module in `extras/`.

| Seam | You provide | Default |
| --- | --- | --- |
| `Clock` | Time source | Real clock |
| `FontProvider` | Font loading, size ladder, rebuild | Bridge default (2.7) |
| `ImageProvider` | Texture upload and lookup | Placeholders |
| `IconProvider` | Icon path sets | Bundled Lucide subset |
| `TextShaper` | Shaping, bidi, line breaking | ImGui text |
| `BackdropProvider` | Render-to-texture blur | Tier 0 frosted look |
| `LayoutDelegate` | A custom layout algorithm | None |
| `Painter` | Custom drawing for a node | None |
| `Behavior` and gesture recognizer | Custom interaction | None |
| `Curve`, `Spring`, `Lerp` traits | New easing or value types | Built-ins |
| `ThemeSource` | Theme loading | Built-in themes |
| `A11yBridge` | OS accessibility publishing | Tree only |
| `HapticSink`, `SoundSink` | Feedback | None |
| `Localizer` | Strings, plurals, formats | Identity |
| `Allocator` | Arena and page memory | `malloc` |
| `Logger` | Diagnostics | stderr |

## 16. Milestones

The order is fixed and strictly sequential. A milestone is done only when its exit criteria pass in CI and in the gallery.

| # | Milestone | Contents | Exit criteria |
| --- | --- | --- | --- |
| M0 | Skeleton | Repo, CMake targets, ImGui bridge for both profiles, arena, state store, clock, math, headless test harness | An empty frame allocates nothing; unit tests green on three operating systems |
| M1 | Motion | Values, curves, tweens, springs, keyframes, timelines, scheduler, `Animate` | Retarget-continuity test passes; frame-rate independence at 30, 60 and 144 Hz; `NeedsFrame()` false when settled |
| M2 | Nodes, style, layout | Node tree, IDs, style resolution, states, transitions, all layout modes, scroll containers, text measurement | 2,000-node layout within 12.1; transition retarget test; goldens for every mode |
| M3 | Paint | Render list, translator, shapes, squircles, mesh shadows, gradients, rect and convex clips, transforms, opacity, Tier 0 materials, text, icons, images | Gallery paint page matches goldens; zero steady-state allocations; paint within 12.1 |
| M4 | Input | Hit testing, capture, gesture arena, focus, keyboard, scroll physics, text-edit behavior, drag and drop | Scroll-feel test matches the numbers in 7.7; mouse, touch and keyboard parity tests |
| M5 | Layers and behaviors | Layer stack, portals, anchoring, modal scopes, the behavior set, Viewport, Router, Presence, animated layout | Enter, exit and shared-element demos are interruptible; node-editor reference app (17) pans and zooms at refresh rate |
| M6 | Themes and Kit core | Token trees, four built-in themes, JSON themes, runtime switch, first Kit components (actions, text input, display, lists) | Definition of Done met per component; appearance switch cross-fades without flashes |
| M7 | Kit complete | Remaining catalog categories (navigation, overlays, pickers, tables, trees, dock) | Definition of Done met per component; reference apps 1 and 3 build from public API only |
| M8 | Platform polish | Accessibility tree and AccessKit bridge, IME, DPI and font rebuild, RTL, localization, touch, Android and Web builds | Gallery runs on Windows, Linux and macOS; Android and Web build; audits green |
| M9 | Tier 1 and tools | BackdropProvider (OpenGL3, Vulkan), inspector, animation debugger, theme editor, record and replay, C API | Full-screen blur within 0.5 ms; tools usable in the gallery; C API example runs |
| M10 | Release | Docs, samples, amalgamation, packaging, API freeze at 1.0 | Every 14 gate green; all five 17 apps pass; two independent integrations verified (SDL3 + OpenGL, GLFW + Vulkan) |

## 17. Proof of generality

The engine is general if five very different applications can be built from the public API alone. Each one is built with no private access; if one needs something missing, that thing is added as public API first (0.2) and the app is rebuilt on it.

| # | Reference app | What it stresses |
| --- | --- | --- |
| 1 | Settings-style app | Apple feel: grouped lists, switches, navigation stack with large titles, sheets, search, appearance switching |
| 2 | Node editor | Viewport pan and zoom, custom painters, ports and wires, drag, minimap, thousands of nodes |
| 3 | Docked tool | Panels, tabs, splits, sortable tables, trees, property inspector, command palette, shortcuts |
| 4 | Game HUD and inventory | A second context drawn over a 3D scene or onto a texture, injected input, gamepad navigation, drag and drop, animation of gameplay values |
| 5 | Dashboard and timeline | Virtualized tables, chart painters, scroll-linked animation, one layout responsive from phone to desktop widths |

All five must run at the display's refresh rate and pass the accessibility audit (14.8).

**Apple-smooth acceptance.** The Kit under the iOS and macOS themes must satisfy every item:

1. Every tap target reacts on the same frame (scale about 0.97 plus a tint) and releases with a spring.
2. Every spring preserves velocity when retargeted.
3. Scrolling has momentum, rubber banding and snapping that match 7.7 at 60 and 120 Hz.
4. Corners are continuous and shadows are soft and layered.
5. Appearance switches cross-fade with no flash.
6. Sheets, popovers and navigation are gesture-driven and interruptible.
7. Text is crisp at every supported scale, with a clear hierarchy.
8. Idle CPU and GPU use is near zero.
9. Reduce motion, text scale and high contrast are honored.
10. The gallery's stress page holds the 12.1 budgets.

## 18. Plan integrity

This document is the single source of truth, and work is organized by  number: a change that cannot cite a  does not merge. If a need seems to conflict with a , it is solved with a seam (15), a new Kit component (10.4), a theme (4), or a new module in `extras/`. None of those changes a rule in 0, the ten primitives in 1, or any contract in 2 to 9. The Kit catalog, themes, extras and tools are open-ended by design; everything else is fixed.
