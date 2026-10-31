# Instructions for GPT: make Lumen look like the reference

Copy everything below the line into ChatGPT, and attach `docs/reference/reference_toasts.png` and `docs/reference/reference_dialog.png`. This file is the look spec; `docs/GPT_HANDOFF.md` is the engineering work order. Do both together: the handoff says **what to build and in what order**, this file says **exactly how it must look and move**.

---

You are continuing the Lumen UI Engine. Read `Lumen_UI_Engine_Master_Plan_No_Section_Signs.md`, `README.md`, `docs/MILESTONE_STATUS.md` and `docs/GPT_HANDOFF.md` first.

## The goal

The two attached images are the target: a flat, soft, Material-3-style look with pill buttons, tonal cards, dark floating toasts with pastel icon badges, a modal dialog over a fading scrim, and small springy motion everywhere. The engine already has the machinery (layout, motion, events, shadows). **The look comes from tokens, metrics and motion rules, not from more engine features.** Your job is to encode the values below as a theme plus a small widget Kit, then prove it matches the references by rendering pictures.

Non-negotiable rules:
- Nothing visual is hard-coded in `core/`. Every value below lives in `Theme` tokens (a new `Material` theme) or in the Kit's style presets, so another theme can restyle the same widgets.
- Widgets live in `kit/` (target `lumen_kit`) and use only public API. If a widget needs something the core lacks, add it to the public API first.
- Do not invent your own colours, radii, durations or easings. Use the numbers here. If you think a value is wrong, say so in your report and keep the spec value until the owner agrees.
- You must render screenshots and look at them. Use `tools/headless_raster.h` and `tools/screenshot_example.cpp` (no window or GPU needed). Do not claim anything "matches" without a screenshot beside the reference.
- Time-dependent tests and screenshots use `Lumen::ManualClock`, advanced every frame. `ImGui::SetNextWindow*` calls go after `ImGui::NewFrame()`.
- Keep the arena budget: do not add per-node data that every node pays for (see `docs/MILESTONE_STATUS.md`, memory budget note).

## 1. Colour roles (Material 3 baseline, light)

Add semantic roles to `Theme` (keep the existing ones working) and fill a new `ThemeKind::Material`. Do not look up colours by name inside widgets; widgets ask for a role.

| Role | Hex | Used for |
| --- | --- | --- |
| `surface` | #FEF7FF | window / page background |
| `surface_container` | #F3EDF7 | cards |
| `surface_container_high` | #ECE6F0 | dialog background, popups |
| `surface_container_highest` | #E6E0E9 | switch track (off), slider/frame background, progress track |
| `on_surface` | #1D1B20 | primary text |
| `on_surface_variant` | #49454F | secondary text, captions, dialog body |
| `outline` | #79747E | outlined button border, switch (off) border and thumb |
| `outline_variant` | #CAC4D0 | hairlines, grid lines |
| `primary` | #6750A4 | filled buttons, switch (on) track, links, selected state |
| `on_primary` | #FFFFFF | text/icons on primary |
| `primary_container` | #EADDFF | selected chips, highlights |
| `secondary_container` | #E8DEF8 | tonal buttons |
| `on_secondary_container` | #1D192B | text on tonal buttons |
| `inverse_surface` | #322F35 | **toast background** (dark card on a light page is what makes toasts pop) |
| `inverse_on_surface` | #F5EFF7 | toast title text |
| `inverse_primary` | #D0BCFF | toast action text ("UNDO") |
| `scrim` | black, alpha 0.36 | dialog scrim |

Status accents for toast icon badges (pastel, they sit on the dark toast): info #8AB4F8, success #81C995, warning #FDD663, error #F28B82. Glyph colour on the badge: #202124.

Hover and pressed states are **state layers**, not new colours: mix the content colour over the container at 8% for hover and 8% for pressed (filled: mix white; tonal: mix `on_secondary_container`; outlined and text buttons: `primary` at alpha 0.08 hover, 0.06 pressed). Provide a `state_layer(base, content, amount)` helper in the Kit.

## 2. Shape, spacing, type

- **Radius scale tokens:** `radius_small` 12, `radius_medium` 16, `radius_large` 20 (cards), `radius_xlarge` 28 (dialogs), `radius_pill` = height / 2 (buttons, switch track, segmented items).
- **Spacing scale tokens:** 4, 8, 12, 16, 24. Card padding 18 x 16. Gaps between controls 10 to 12. Margin from screen edge for toasts 24.
- **Cards are flat tonal fills** (`surface_container`), no border, no shadow. Depth comes from the fill, not from outlines.
- **Typography:** one family, Roboto Medium (`imgui/misc/fonts/Roboto-Medium.ttf`, Apache-2.0). Sizes: caption/secondary 14, body 16, heading 20, title 24. Never scale text at runtime; bake each size. This is plan 2.7 (the font ladder); implement it. Snap font sizes to the nearest rung, and re-bake the ladder at the framebuffer scale so text stays crisp on HiDPI.
- Text is left-aligned, `on_surface`; captions use `on_surface_variant`. Only two or three sizes per screen.

## 3. Component specs

**Buttons** (`FilledButton`, `TonalButton`, `OutlinedButton`, `TextButton`)
- Height 40, pill ends. Horizontal padding 24 (text button 14). Label 16, centred.
- Filled: `primary` / `on_primary`. Tonal: `secondary_container` / `on_secondary_container`. Outlined: transparent, 1 px `outline` border, `primary` text. Text: transparent, `primary` text.
- Hover: state layer fades in over 0.15 s `OutCubic`. Press: state layer, plus a 1 px inward squish, over 0.10 s `OutCubic`.
- Filled and tonal lift on hover with a shadow: blur 12, offset_y 4, shadow animating in with the same 0.15 s transition. Resting state has no shadow.
- **Ink splash on press:** a rounded rect starting 4 x 4 at the pointer position that grows to the full button rect over 0.55 s `OutQuad`, with alpha 0.26 x (1 - t). White on filled, `primary` on the others. It stays inside the button, so no clipping is needed. Driven by the motion system; theme tokens for duration, curve and alpha.

**Switch**
- Track 52 x 32, pill. Off: `surface_container_highest` fill with a 2 px `outline` border inside, thumb `outline` colour, radius 8, centre x = 16. On: `primary` fill, no border, thumb `on_primary`, radius 12, centre x = 36. Pressed thumb radius +2.
- Thumb position animates 0.32 s with `OutBack` (a small overshoot). Colours use the clamped progress. A 20 px radius halo (alpha 0.08 on hover, 0.08 on press) sits behind the thumb.
- Label to the right with a 14 px gap, vertically centred. The whole row is clickable.

**Card** - `surface_container`, radius 20, padding 18 x 16, no shadow. Section heading 20, caption 14 under it, 2 px extra gap after.

**Toast** (the centrepiece; build on Portal + Presence + `ShadowStyle`)
- Width 344. Padding 14. Radius 16. Background `inverse_surface`.
- Left: a 28 px circular badge in the status accent, with a glyph drawn in #202124: check (success), "i" (info), "!" (warning), cross (error). Gap 12 to the text column.
- Text column: title 16 in `inverse_on_surface`, message 14 in `outline_variant`, 3 px apart. Both wrap to the column width.
- Right side: close button, a 28 px hit area inset 8 from the edge, an X glyph that brightens on hover with a faint 10% circle. Optional text action ("UNDO") to its left: 14 px, `inverse_primary`, 28 px high hit area, a 12% pill background on hover.
- Bottom: a 3 px countdown bar, inset 14 from the sides, 12 px below the content. Track `inverse_on_surface` alpha 0.12, fill in the status accent, shrinking linearly over the duration (default 3.5 s). **The timer pauses while the pointer is over the toast.**
- Shadow: blur 16, offset_y 6, black alpha about 0.30. It must visibly spill outside the card (this is the "floating" look).
- Stack: newest nearest the anchor edge, 10 px gap, max 4 visible (oldest is dismissed). Anchors: bottom-centre (default), bottom-right, top-right, 24 px from the viewport edge.
- Motion (all through the motion system and theme tokens):
  - Enter: 0.55 s `OutBack`. Bottom-centre rises 36 px; right anchors slide in 70 px from the right. Opacity ramps over the first ~23% of the enter time. The icon badge scales with the same `OutBack` value, clamped to 1.18.
  - Exit: 0.26 s `InCubic`, fading while sliding (22 px down for bottom-centre, 90 px right for right anchors).
  - Reflow: when a neighbour leaves or arrives, the others glide to their new slots in 0.38 s `FastOutSlowIn`. It must be interruptible (a retarget mid-glide continues from the current position, no jump).

**Alert dialog**
- Width 312, padding 24, radius 28, background `surface_container_high`. Centred in the viewport.
- Title 24, then 16 gap, body 16 in `on_surface_variant`, then 24 gap, then a right-aligned row of two text buttons with 8 gap ("Cancel", then the confirm action).
- Scrim: `scrim` colour, fading with the dialog. Click on the scrim = Cancel. Esc = Cancel.
- Shadow behind the card: blur 40, offset_y 10, black alpha about 0.30.
- Motion: enter 0.32 s `FastOutSlowIn`, fade in while rising 28 px. Exit 0.18 s `InQuad`. Scrim and card share the same progress. Content stays alive until the exit finishes (Presence).

**Segmented / selector buttons** (position picker in the reference): three 112 x 36 pill buttons, selected = filled, others = outlined.

**Plot / demo widgets** are not required. The reference images only need what is listed above.

## 4. Easing and duration tokens

Add these to `Theme` as named curves/durations. Widgets reference tokens; they never contain numbers.

| Token | Curve | Duration |
| --- | --- | --- |
| `motion_hover` | OutCubic | 0.15 s |
| `motion_press` | OutCubic | 0.10 s |
| `motion_switch` | OutBack (c1 = 1.70158) | 0.32 s |
| `motion_splash` | OutQuad | 0.55 s |
| `motion_enter_pop` | OutBack | 0.55 s |
| `motion_exit_fast` | InCubic | 0.26 s |
| `motion_reflow` | FastOutSlowIn = cubic-bezier(0.4, 0, 0.2, 1) | 0.38 s |
| `motion_dialog_in` | FastOutSlowIn | 0.32 s |
| `motion_dialog_out` | InQuad | 0.18 s |
| `motion_default` | FastOutSlowIn | 0.20 s |
| `motion_decelerate` | cubic-bezier(0.05, 0.7, 0.1, 1) | for large entrances if needed |

Rules of thumb that make it feel right: entrances overshoot slightly or decelerate; exits are faster than entrances (about half); opacity changes finish before the movement does; every state change is interruptible; reduce-motion shortens everything instead of removing it.

## 5. Mapping the spec onto what already exists

- `ShadowStyle` (blur, spread, offset, colour, hover lift) is implemented. Calibrate the shadow numbers above against the reference images by eye; the numbers are the starting point, the picture is the truth.
- `Theme` currently has Dark, Light and High Contrast. Add `ThemeKind::Material` with the roles above and make it the gallery default. Keep the others working; High Contrast must still disable shadows.
- `NodeStyle` is size-sensitive. Add only what the Kit needs, behind optional pointers where it is not used by every node.
- Hover and pressed state currently exist only for `Button` nodes. Generalise it first (handoff item 1) because cards, list rows, the switch row and the segmented items need it.
- Portal and layers are not wired in. Toast and dialog depend on handoff item 2.

## 6. Build order for the look (combine with the handoff order)

1. Theme: colour roles, radius and spacing scales, motion tokens, `Material` theme. Test: every role is populated, themes cross-fade, High Contrast unchanged.
2. Font ladder (14/16/20/24) with re-bake at framebuffer scale. Test: text measures identically across frames and rungs are chosen by size.
3. State layers + ink splash + the four button styles. Screenshot: all four buttons at rest, hover, pressed, mid-splash.
4. Switch. Screenshot: off, on, mid-transition (thumb overshoot visible), pressed.
5. Portal, then Toast and AlertDialog with the exact metrics and motion above.
6. A gallery page that reproduces the reference screens: the popups card with the four toast buttons, "Toast with undo", "Alert dialog", the position selector, plus the dialog open over the scrim.

## 7. Acceptance checklist (report each line with evidence)

For every item, produce a PNG from `tools/headless_raster.h` and say what you compared.

- [ ] Screenshot of the gallery next to `reference_toasts.png`: same page background, tonal cards, pill buttons, same type sizes, same toast look. Toast width 344; icon badge 28; progress bar 3 px; shadow spills outside the card.
- [ ] Screenshot of the dialog open next to `reference_dialog.png`: scrim at about 36%, dialog radius 28, title 24, buttons right-aligned.
- [ ] Mid-animation frames (use `ManualClock`, advance by 1/60 per frame): toast at 5, 15 and 30 frames after creation; switch mid-toggle; button mid-splash; dialog at 5 and 15 frames. Say whether overshoot and fade timing look right.
- [ ] Tests (names listed): toast timer pauses while hovered; max 4 visible; neighbours glide and the reflow is interruptible; exit keeps content alive for its duration; Esc and scrim-click dismiss the dialog; focus returns to the opener; reduce-motion shortens but does not skip.
- [ ] Sanitizer run (`-fsanitize=address,undefined`, `-Wall -Wextra -Wpedantic -Wshadow`) with the real output pasted.
- [ ] Arena budget unchanged or better (state the bytes for the 2,001-node test).
- [ ] A short list of every place the render differs from the reference and why.

## 8. Things that will make it look generic (avoid)

- Borders and outlines on every box. Use fills and shadows; borders only for outlined buttons and the switch (off).
- Default engine colours or a blue "accent" that is not `primary` from the Material theme.
- Linear or instant state changes. If something changes visibly, it animates with a token curve.
- Hard-coded numbers inside widget code. They belong in the theme.
- Many font sizes, many radii, many accent colours on one screen.
- Shadows on everything. Only floating things (toasts, dialogs, hovered filled buttons) cast shadows.
- Blurry text. Fonts must be baked at the sizes used and drawn 1:1.

## 9. How to report back

For each step: files changed, new tests (names), exact sanitizer output, and the screenshots with a one-line verdict each. If a screenshot does not match the reference, say so plainly and fix it before moving on. Mark anything partial as partial in `docs/MILESTONE_STATUS.md`.
