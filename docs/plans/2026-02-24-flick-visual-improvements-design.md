# flick: Visual Improvements Design

Rename of warpd fork to "flick" + 10 visual/UX improvements.

## Rename

Surface-only. Binary → `flick`, version string → `flick v1.x`, process/lockfile/usage strings updated. Internal code stays `warpd`. Config path stays `~/.config/warpd/`.

## Improvements

### Group A: Cursor Motion (do together — shared velocity/interpolation state)

**1. Motion-stretched core.** Pass `velocity_x, velocity_y` (not just magnitude) to `macos_draw_cursor`. Core dot drawn as oval: stretched along velocity direction proportional to speed. At rest, perfect circle.

**2. Spring overshoot.** On teleport (>300px jump via hint/top/bottom/start/end), set interpolation target 8% past destination, then immediately correct to real position. Lerp naturally creates spring-back.

**3. Acceleration speed lines.** Pass `accelerating` flag to `redraw()`. When active, draw 2-3 fading lines trailing behind cursor using velocity direction. Use `screen_draw_box` with thin dimensions and alpha.

**9. Idle breathe.** Track `idle_ms` (reset on any movement). After 500ms stationary, blend pulse: 3 Hz / 15% amplitude → 1 Hz / 25% amplitude. Smooth transition over ~300ms.

### Group B: State Feedback (independent)

**4. Cursor shape morphing.** Enum: circle (normal), diamond (drag), ring (scroll). In `macos_draw_cursor`, switch path based on shape. Morph = cross-fade old→new over ~80ms on state change.

**5. Screen edge pulse.** In `redraw()`, detect cursor within 2px of screen edge. Draw fading line along that edge. Duration ~150ms, reuses `screen_draw_box` with alpha.

### Group C: Hint/Grid Animations (independent)

**6. Hint cascade wave.** In hint `draw_hook`, compute each hint's distance from cursor. Per-hint delay: `delay_ms = distance * 0.08`. Hints closer to cursor appear first, edges last. Total ~120ms spread.

**7. History ghost dots.** In `history_hint_mode()`, before `hint_selection`, draw faint circles at each history position via `screen_draw_circle` at 20% alpha.

**10. Grid sweep animation.** Track grid appear time. Grid lines clip from edges inward over ~100ms. First frame: only edge lines visible. Final frame: all lines at correct positions.

### Group D: Adaptive Contrast

**8. Smart sampling.** Use `CGWindowListCreateImage` to capture 20x20px around cursor every 200ms or when cursor moves >30px. Compute average luminance. If luminance > 0.6, use dark cursor variant; otherwise keep configured color. Cache result. macOS-only (other platforms skip).

## Files Modified

| Area | Files |
|------|-------|
| Rename | Makefile, src/warpd.c, man page |
| Group A | src/normal.c, src/platform.h, src/platform/macos/macos.{h,m}, src/platform/macos/screen.m, stubs |
| Group B | src/normal.c, src/platform/macos/macos.m |
| Group C | src/platform/macos/hint.m, src/hint.c, src/grid.c |
| Group D | src/platform/macos/screen.m or new file, src/normal.c |
