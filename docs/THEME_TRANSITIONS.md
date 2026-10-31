# Appearance transitions

`SetTheme(context, ThemeKind)` and `SetTheme(context, theme)` use the target theme's `color_transition` timing. Palette colors are interpolated in OKLab; spacing, typography sizes, button metrics, focus metrics, and other scalar visual tokens interpolate linearly. Non-finite easing output falls back to linear progress, and eased progress is bounded to `[0, 1]` because theme tokens should not overshoot.

The transition is driven by Lumen's context motion clock, not wall-clock time. This makes it deterministic under `ManualClock`, pauseable through `SetMotionTimeScale`, and compatible with `NeedsFrame()`. `IsThemeTransitioning()` reports whether tokens still have a transition to complete. Repeated calls retarget from the currently displayed theme, avoiding a flash back to the previous endpoint.

Enabling reduce-motion completes an active theme transition and requests repaint. Setting the initial theme before the first clocked frame applies it immediately: no valid elapsed-time baseline exists yet, and treating an arbitrary pre-frame time as an animation start would make initial startup depend on the host clock epoch.

`BeginFrame` validates the viewport before resetting the frame arena or advancing state. Bounds must be finite and non-inverted; scale must be finite and positive. Safe-area insets are clamped to the viewport extent so a malformed oversized inset cannot create a negative layout rectangle. Invalid geometry is rejected with `false` and does not increment the frame counter.
