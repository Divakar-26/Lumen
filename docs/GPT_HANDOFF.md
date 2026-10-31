# Handoff prompt for GPT (copy everything below the line)

Companion file: `docs/GPT_LOOK_INSTRUCTIONS.md` holds the exact look spec (colours, metrics, motion tokens, acceptance checklist) and `docs/reference/` holds the target screenshots. Give GPT both files; this one is the build order, that one is how it must look.

---

You are continuing the Lumen UI Engine (C++17, on top of Dear ImGui). Read `Lumen_UI_Engine_Master_Plan_No_Section_Signs.md`, `README.md` and `docs/MILESTONE_STATUS.md` first. The core is solid: 4 test executables pass under ASan/UBSan with `-Wall -Wextra -Wpedantic -Wshadow` clean. **The core now has a visible integrated portal gallery. The next goal is to build effects and Kit widgets through the public API.** The owner's reference point is a small tween playground (separate project) whose popups felt "extraordinary" in minutes: dark Material toasts that slide in with an overshoot, an icon badge that pops, a countdown bar that pauses on hover, a stacking list that glides when a neighbour leaves, a dialog with a fading scrim, pill buttons with an ink splash, and a switch whose thumb springs. Your job now is to make Lumen able to produce that feel **through its own public API**, in the order below. Do not add new infrastructure that no visible feature needs.

## Hard rules (do not break these)

1. Follow the charter. Nothing hard-codes a look: colours, radii, durations, curves and springs come from `Theme` or `NodeStyle`. The core never learns what a "toast" is; toasts, dialogs, switches and ripples belong in a Kit built only from public API (charter 0.1 and 0.2).
2. Only `core/imgui_bridge.cpp` includes `imgui_internal.h`.
3. Tests that involve time use `Lumen::ManualClock` and advance it every frame. A real clock does not advance between instant test frames, so animations look frozen and tests lie.
4. Every change ships with a test, the sanitizer build stays clean, and warnings stay at zero. Never write "tests pass" without running them; paste the real output.
5. Set `NodeStyle` members by name. Members are ordered by size class on purpose (no padding); keep that when you add fields.
6. Update `docs/MILESTONE_STATUS.md` honestly: say "partial" when something is partial.
7. Never claim a visual result you did not render. Use `tools/headless_raster.h` and `tools/screenshot_example.cpp` (render draw lists to a PNG with no window) or the X11 gallery, and look at the picture.

## Facts you must work with

- Memory budget is the biggest structural risk. Two thousand plain labels use 1,040,720 of the 1,048,576 byte frame arena, about 520 bytes per node (432-byte `Node` embedding a full 256-byte `NodeStyle`, a 72-byte `RenderCommand`, copied text). `test_ui` asserts `arena_bytes_used < 1 MiB` for 2,001 nodes. Any new per-node data must be free or optional. Optional features go behind a pointer that `BeginNode` copies into the arena (see `NodeStyle::shadow`).
- Hover/pressed selectors are generalized to pointer-enabled containers; `GetNodeState()` exposes the last completed frame state.
- `LayerStack` is integrated with Context-managed portals: portal roots render in z-order, are hit-tested before the base tree, and modal portals provide scrim, input gating, Tab focus trapping and focus restoration. See `docs/LAYERS.md`.
- `Presence` drives layer enter/exit and timed dismissal, including continuous reversal. `Router` remains standalone and is not yet wired into a page/view lifecycle.
- Removing a node's shadow snaps it off (no fade-out), because a node that stops declaring a shadow has no animation state to read.

## Current source snapshot (2026-10-31)

The uploaded M6/M7 interaction source has since been extended with the first integrated portal milestone. `docs/MILESTONE_STATUS.md` and `docs/LAYERS.md` describe the current behavior and its limitations. Portal work item 2 below is implemented as a first bounded slice, but remains partial for nested portals, callback/RAII declaration, generalized focus scopes and accessibility. Do not repeat the old statement that the project has no portal renderer.

## Work items, in order

### 1. Generalise interaction state (small, unblocks everything)
- Give every node with `pointer_events` hover and pressed state (not just buttons), exposed as style selectors: `hover_background`, `pressed_background` already exist, so make them work on any node, and add `hover_opacity`/`hover_shadow` equivalents only if needed.
- Add `GetNodeState(context, id)` (hovered, pressed, focused) so Kit code can read it.
- Test: hover and press on a non-button container changes its resolved fill; leaving restores it; the transition is interruptible.

### 2. Portal and layers wired into the frame — implemented in the current source (M8)
- `Portal(layer, key, fn)`: declare overlay content from anywhere in the tree; it is laid out against the viewport (not its parent), painted above everything in `LayerStack` order, and hit-tested first.
- Modal layers block input below them (`BlocksInput` already exists); Escape and click-outside dismissal go through the existing dismissal policies.
- Modal focus trap and restore: Tab cycles inside the top modal; closing returns focus to what had it.
- Each layer gets a `Presence`: enter and exit are animations, exit keeps the content alive until it finishes, reversal is continuous.
- Tests: paint order, input gating, dismissal, exit animation keeps content for its duration, focus restore.

### 3. Ink splash and general "effects on a node" — implemented (custom painter hook + `Kit::BeginInkSurface`; see MILESTONE_STATUS)
- A node-level ripple: a rounded shape that grows from the press point and fades (the playground grew a rounded rect from the pointer to the button bounds; no clipping tricks needed). Driven by the motion system, theme tokens for duration and curve.
- If this needs a custom painter hook (plan 6.9), build the hook first and have the ripple use it, so users can write their own.

### 4. Kit v0 (public API only, in a separate target `lumen_kit`) — implemented: Material theme, the four buttons, Switch, Card, Segmented, AlertDialog and Toast stack; the gallery is the Kit showcase. Remaining look-spec gaps are listed in MILESTONE_STATUS
Build these in `kit/`, with no access to core internals:
- `Toast` stack: slide-and-fade enter with overshoot, icon badge pop, countdown bar that pauses while hovered, action button, close button, stacking that animates when an item leaves, max visible count, three anchor positions. Built on Portal, Presence and `ShadowStyle`.
- `AlertDialog`: fading scrim, card that rises while fading in, confirm and cancel results, Esc and scrim-click dismissal.
- `Switch`, `FilledButton`/`TonalButton`/`OutlinedButton`/`TextButton` with hover lift and ink splash, `Card`, `Segmented` control.
- Every Kit widget takes its look from a theme token set (add a Material-like theme next to Dark, Light and High Contrast; do not hard-code it in widgets).
- Gallery: replace the diagnostics-style panels with a page that shows these widgets. A person should open it and want to click things.

### 5. Fix the memory budget properly (before the tree gets bigger)
- Option A: split `Node` into hot data (id, links, rect, flags) and cold data (full style) so layout-only nodes cost less.
- Option B: chunked arena that grows up to a configured cap and keeps its chunks between frames (still zero steady-state allocation). Keep `arena_failed_allocations` semantics at the cap.
- Either way, tighten `test_ui` to assert a bytes-per-node number, not just "under 1 MiB".

### 6. Smaller known gaps (after the above)
- Shadow fade-out when a node drops its shadow; optionally clip the shadow to outside the node for translucent fills.
- Text: the font ladder (`SetFontLadder`) is done. Still missing: re-baking at the framebuffer scale (`RebuildFontTexture` hook, plan 2.7) and an ellipsis/overflow policy.
- Plan 8 items still open: focus scopes, shortcuts, drag and drop, gesture arena. Single-line text editing exists (`Kit::TextField`); multiline, undo/redo and IME placement do not.
- Windows and macOS builds, a second ImGui version in CI.

## How to report back
For each item: what changed (files), the new tests (names), the exact output of the sanitizer test run, and a screenshot or draw-list render of the visible result. If something is partial, say so in `docs/MILESTONE_STATUS.md` and in your message. Do not move to the next item until the current one is tested and rendered.
