# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Lumen is a C++17 declarative/retained UI engine layered on Dear ImGui's public API. The host owns the window, event loop, renderer, fonts and ImGui context; Lumen builds a node tree per frame, lays it out, animates it, routes input and emits `ImDrawList` commands.

## Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
ctest --test-dir build -R lumen_kit_tests --output-on-failure   # one suite
./build/lumen_ui_tests                                           # run a suite binary directly
./build/lumen_gallery                                            # X11/OpenGL sample host (needs X11; Xvfb when headless)
```

Test suites (CTest name → source): `lumen_m1_tests` → `tests/test_main.cpp` (foundation/motion), `lumen_ui_tests` → `tests/test_ui.cpp`, `lumen_behavior_tests` → `tests/test_behaviors.cpp`, `lumen_layer_tests` → `tests/test_layers.cpp`, `lumen_kit_tests` → `tests/test_kit.cpp`. `test_ui` gets `LUMEN_SOURCE_DIR` for font files. In test hosts, call `ImGui::SetNextWindow*` after `ImGui::NewFrame()`, or the window is a tiny default box that CPU-clips text. Each is a plain executable with a local `CHECK` macro and a `main()` that calls `Test*` functions in order; there is no framework and no per-test filter, so to run a single test, temporarily narrow `main()` or add the new `Test*` call there.

Sanitizer build (expected to stay clean):

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DLUMEN_BUILD_GALLERY=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize --parallel 4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize --output-on-failure
```

Warnings are `-Wall -Wextra -Wpedantic -Wshadow` and must stay at zero. Headless visual check: `cmake --build build --target lumen_interaction_screenshot` (run from repo root so it finds `imgui/misc/fonts/Roboto-Medium.ttf`); `tools/headless_raster.h` rasterizes ImGui draw data to PNG without a window or GPU.

## Architecture

- **Targets:** `lumen_core` (`core/`, public headers in `include/lumen/`), `lumen_kit` (`kit/`, headers in `kit/include/lumen/kit/`), vendored `imgui/` (`lumen_imgui`, aliased `imgui::imgui`; disable with `-DLUMEN_BUILD_BUNDLED_IMGUI=OFF`), optional `lumen_gallery` (`examples/`). The core never depends on X11/OpenGL.
- **Kit is a client of the public API only** (ink splash via a custom painter + wrapped event handler, button variants on top of core `Button`). Kit code must not include anything from `core/`. Look (colours, radii, durations, curves, springs) comes from `Theme` or `NodeStyle`; the core never learns what a toast/switch/ripple is.
- **Only `core/imgui_bridge.cpp` includes `imgui_internal.h`.**
- **Frame lifecycle** (`core/context.cpp`): `BeginFrame` validates the viewport, advances the clock/theme transition and clears `needs_frame`; user code declares nodes; `EndFrame` → `detail::FinishNodeFrame` (`core/node.cpp`) does inheritance, measure, layout, scroll clamping, portal layout, hit testing, event dispatch, hover/press resolution, then builds a frame-arena `RenderCommand` list and translates it to `ImDrawList`. Base tree paints first, then portals in `LayerStack` order (modal scrim before modal content). Event handlers and custom painters run inside `EndFrame` while the frame is still active; node/portal declaration is closed during painting.
- **Memory model:** per-frame data (nodes, copied text, render commands, optional style blobs) comes from a fixed-capacity frame arena (`core/arena.*`, default 1 MiB); persistent data lives in a fixed-capacity state pool (`core/state_store.*`) accessed via `State<T>(context, id)` (trivially copyable types only, TTL eviction). No steady-state heap allocation.
- **Arena budget is the main structural constraint.** `test_ui` asserts 2,001 nodes fit in 1 MiB (~520 bytes/node; `Node` 432 bytes embeds a full `NodeStyle`). New per-node data must be free: put optional features behind a pointer in `NodeStyle` that `StartNode` copies into the arena (see `shadow`, `custom_paint`). `NodeStyle` and `Node` members are ordered by size class to avoid padding — keep that ordering and always set `NodeStyle` fields by name.
- **Identity:** node IDs are `MakeId(parent_id, key)`; the root ID comes from `GetFrameInfo(ctx)->root_id`. Duplicate sibling keys reject the frame.
- **Motion** (`include/lumen/motion.h`, `core/motion.cpp`): curves, tweens, springs, keyframes, controllers, implicit `Animate(...)` keyed by state IDs; reduce-motion and time-scale are per context. Theme changes interpolate (OKLab colours) via `BlendTheme` in `context.cpp` — new `Theme` colour tokens must be added there.
- **Layers/portals:** `LayerStack` and `ResolveAnchorPlacement` (`include/lumen/layers.h`) plus Context-managed `OpenLayer`/`BeginPortal`/`EndPortal`; `Presence` drives enter/exit. See `docs/LAYERS.md`.

## Project rules

- `Lumen_UI_Engine_Master_Plan_No_Section_Signs.md` is the frozen roadmap; `docs/MILESTONE_STATUS.md` records what is actually implemented (update it honestly, saying "partial" where partial). `docs/GPT_HANDOFF.md` holds the ordered work items and `docs/GPT_LOOK_INSTRUCTIONS.md` the visual spec.
- Time-dependent tests must use `Lumen::ManualClock` and advance it every frame; the real clock does not move between instant test frames.
- Every behaviour change ships with a regression test; don't claim a visual result without rendering it (headless raster or gallery).
- This working copy is currently not a git repository (no `.git`/remote).
