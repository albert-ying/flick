# Flick Visual Improvements Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Rename warpd fork to "flick" (surface-only) and add 10 visual/UX improvements: motion-stretched cursor, spring overshoot, speed lines, idle breathe, cursor shape morphing, screen edge pulse, hint cascade wave, history ghost dots, grid sweep animation, and adaptive contrast sampling.

**Architecture:** All rendering goes through the `struct platform` function pointer table. macOS has the rich implementation (`macos_draw_cursor` in `macos.m`); Linux X11/Wayland/Windows have stub fallbacks. New animation state lives as file-static variables in `normal.c` (cursor effects) or the relevant mode file. The 10ms event loop in `normal_mode()` drives animations at ~100fps. Drawing hooks are ephemeral — `screen_clear()` wipes all hooks each frame, so all draw data must be re-registered every tick.

**Tech Stack:** C99, Cocoa/AppKit (macOS), X11/Wayland stubs (Linux), Win32 stubs (Windows). Build: `make clean && make`.

---

## Task 1: Surface rename to "flick"

Surface-only rename. Binary output → `flick`, version string → `flick v1.x`, usage/lock/log strings updated. Config path stays `~/.config/warpd/`. Internal code stays `warpd`.

**Files:**
- Modify: `Makefile:1` (binary target name)
- Modify: `src/warpd.c:108-113` (usage string)
- Modify: `src/warpd.c:133-135` (version print)
- Modify: `src/warpd.c:70` (lockfile name)
- Modify: `src/warpd.c:79-83` (lockfile error message)
- Modify: `src/warpd.c:312` (startup log)
- Modify: `warpd.1.md` (man page title)

**Step 1: Update Makefile binary target**

In `Makefile`, the binary is currently output to `bin/warpd`. The platform `.mk` files reference `bin/warpd` as a target. Find all references.

Check `mk/macos.mk` and `mk/linux.mk` for the binary target name. The output binary should become `bin/flick`.

In `Makefile:1`, no change needed (VERSION stays). The binary target is in `mk/macos.mk` or `mk/linux.mk`. Find the line that defines the binary output path and rename `warpd` → `flick`.

**Step 2: Update version string**

In `src/warpd.c:133-135`:
```c
static void print_version()
{
	printf("flick " VERSION"\n");
}
```

**Step 3: Update usage string**

In `src/warpd.c:108`:
```c
	const char *usage =
		"flick: [options]\n\n"
```

**Step 4: Update lockfile**

In `src/warpd.c:70`:
```c
	sprintf(path, "/tmp/flick_%d.lock", getuid());
```

In `src/warpd.c:79-83`:
```c
		fprintf(
		    stderr,
		    "ERROR: Another instance of flick is already running.\n");
```

**Step 5: Update startup log**

In `src/warpd.c:312`:
```c
		printf("Starting flick " VERSION "\n");
```

**Step 6: Update man page title**

In `warpd.1.md`, change the title from `warpd` to `flick` (first line / scdoc header).

**Step 7: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors, binary at `bin/flick` (or `bin/warpd` if mk files not updated — check mk files first)

**Step 8: Commit**

```bash
git add Makefile mk/ src/warpd.c warpd.1.md
git commit -m "rename: surface rename warpd → flick (binary, version, usage, lockfile)"
```

---

## Task 2: Extend platform interface for directional velocity

Several Group A features need directional velocity (vx, vy components) passed to the cursor drawing. Currently only scalar `velocity` is passed.

**Files:**
- Modify: `src/platform.h:74` — add `float velocity_x, velocity_y` to `screen_draw_cursor` signature
- Modify: `src/platform/macos/macos.h:42-52` — add `velocity_x, velocity_y` to `cursor_draw_data` struct + `macos_draw_cursor` declaration
- Modify: `src/platform/macos/screen.m:41-58` — pass through `velocity_x, velocity_y`
- Modify: `src/platform/macos/macos.m:54-56` — accept `velocity_x, velocity_y` params
- Modify: `src/normal.c:179-181` — pass `velocity_x, velocity_y` (computed from lerp step)
- Modify: `src/grid.c:72-78` — pass `0.0, 0.0` for velocity_x, velocity_y
- Modify: `src/platform/linux/X/X.h:99` — update stub signature
- Modify: `src/platform/linux/X/screen.c:94-99` — update stub signature
- Modify: `src/platform/linux/wayland/wayland.h:120` — update stub signature
- Modify: `src/platform/linux/wayland/screen.c:123-128` — update stub signature
- Modify: `src/platform/windows/windows.c:107-112` — update stub signature

**Step 1: Add velocity components to platform.h**

In `src/platform.h:74`, change:
```c
	void (*screen_draw_cursor)(screen_t scr, int x, int y, int size, const char *fill_color, const char *border_color, int border_size, float pulse_hz, float velocity);
```
To:
```c
	void (*screen_draw_cursor)(screen_t scr, int x, int y, int size, const char *fill_color, const char *border_color, int border_size, float pulse_hz, float velocity, float velocity_x, float velocity_y);
```

**Step 2: Update macos.h**

In `src/platform/macos/macos.h`, add `velocity_x, velocity_y` to `cursor_draw_data` struct (after `velocity` field):
```c
	float velocity_x;
	float velocity_y;
```

Update `macos_draw_cursor` declaration:
```c
void macos_draw_cursor(struct screen *scr, NSColor *fill, NSColor *border,
		       float x, float y, float size, float border_size, float pulse_hz, float velocity,
		       float velocity_x, float velocity_y);
```

**Step 3: Update screen.m passthrough**

In `src/platform/macos/screen.m`, update `osx_screen_draw_cursor` to accept and store `velocity_x, velocity_y`, then pass them through in `cursor_draw_hook`.

**Step 4: Update macos.m signature**

In `src/platform/macos/macos.m:54-56`, add `velocity_x, velocity_y` params. Don't use them yet — just accept them.

**Step 5: Track velocity components in normal.c**

In `src/normal.c`, add file-statics:
```c
static float vel_x = 0.0f, vel_y = 0.0f;  /* velocity direction components */
```

In `update_visual_position()`, after computing `dx * LERP_FACTOR`:
```c
	vel_x = dx * LERP_FACTOR * 100.0f;  /* px/sec */
	vel_y = dy * LERP_FACTOR * 100.0f;
```

Update the `screen_draw_cursor` call in `redraw()`:
```c
		platform->screen_draw_cursor(scr, draw_x, draw_y,
				cursz, curcol, curborder, curbordersz, pulse_hz,
				cur_velocity, vel_x, vel_y);
```

**Step 6: Update grid.c**

In `src/grid.c:72-78`, add `0.0, 0.0`:
```c
	platform->screen_draw_cursor(scr,
			x+gw/2, y+gh/2,
			cursz,
			config_get("cursor_color"),
			config_get("cursor_border_color"),
			config_get_int("cursor_border_size"),
			3.0, 0.0, 0.0, 0.0);
```

**Step 7: Update all stubs**

Update X11, Wayland, Windows stub signatures to accept `float velocity_x, float velocity_y` (ignore them).

**Step 8: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors

**Step 9: Commit**

```bash
git add src/platform.h src/platform/macos/macos.h src/platform/macos/screen.m src/platform/macos/macos.m src/normal.c src/grid.c src/platform/linux/ src/platform/windows/
git commit -m "feat: add directional velocity (vx, vy) to cursor draw interface"
```

---

## Task 3: Motion-stretched core (Group A, item 1)

Draw the cursor core as an oval stretched along the velocity direction, proportional to speed. At rest, perfect circle.

**Files:**
- Modify: `src/platform/macos/macos.m:54-97` — reshape core from circle to oval based on velocity direction

**Step 1: Implement oval stretching**

In `macos_draw_cursor`, after computing `core_radius`, add stretching logic:

```c
	/* Motion stretch: elongate core along velocity direction */
	float speed = sqrtf(velocity_x * velocity_x + velocity_y * velocity_y);
	float stretch = 1.0f + fminf(speed / 800.0f, 0.8f);  /* up to 1.8x elongation */

	float core_rx, core_ry;  /* semi-axes */
	if (speed > 5.0f) {
		/* Angle of velocity vector */
		float angle = atan2f(-velocity_y, velocity_x);  /* negative vy for LLO */

		/* Stretched ellipse: major axis along velocity, minor axis perpendicular */
		core_rx = core_radius * stretch;
		core_ry = core_radius / sqrtf(stretch);

		/* Draw rotated oval using NSAffineTransform */
		NSAffineTransform *xform = [NSAffineTransform transform];
		[xform translateXBy:cx yBy:cy];
		[xform rotateByRadians:angle];

		NSBezierPath *corePath = [NSBezierPath bezierPathWithOvalInRect:
			NSMakeRect(-core_rx, -core_ry, core_rx * 2, core_ry * 2)];
		[xform concat];
		[fill setFill];
		[corePath fill];

		if (border_size > 0) {
			[border setStroke];
			[corePath setLineWidth:border_size];
			[corePath stroke];
		}

		/* Restore transform */
		NSAffineTransform *inv = [xform copy];
		[inv invert];
		[inv concat];
	} else {
		/* At rest: perfect circle (existing code) */
		NSRect coreRect = NSMakeRect(cx - core_radius, cy - core_radius,
					     core_radius * 2, core_radius * 2);
		NSBezierPath *corePath = [NSBezierPath bezierPathWithOvalInRect:coreRect];
		[fill setFill];
		[corePath fill];

		if (border_size > 0) {
			[border setStroke];
			[corePath setLineWidth:border_size];
			[corePath stroke];
		}
	}
```

Replace the existing core dot section (lines ~84-96) with this.

**Step 2: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Cursor should appear as oval when moving fast, circle when still.

**Step 3: Commit**

```bash
git add src/platform/macos/macos.m
git commit -m "feat: motion-stretched cursor core (oval along velocity direction)"
```

---

## Task 4: Spring overshoot (Group A, item 2)

On teleport (>300px jump via hint/top/bottom/start/end), set interpolation target 8% past destination, then immediately correct. The existing lerp creates the spring-back effect naturally.

**Files:**
- Modify: `src/normal.c:79-110` — in `update_visual_position()`, add overshoot logic

**Step 1: Implement spring overshoot**

In `src/normal.c`, add state:
```c
static int spring_active = 0;
static float spring_target_x, spring_target_y;
static float spring_real_x, spring_real_y;
```

In `update_visual_position()`, replace the teleport snap block (lines 93-98):
```c
	/* Teleport: hint jump, screen edge, etc. */
	if (dist > 300.0f) {
		/* Overshoot 8% past destination, then correct next tick */
		float dir_x = dx / dist;
		float dir_y = dy / dist;
		float overshoot = dist * 0.08f;

		vx = (float)target_x + dir_x * overshoot;
		vy = (float)target_y + dir_y * overshoot;
		cur_velocity = 0.0f;
		trail_reset();
		spring_active = 1;
		spring_real_x = (float)target_x;
		spring_real_y = (float)target_y;
		return;
	}

	/* Spring correction: lerp back from overshoot to real position */
	if (spring_active) {
		float sdx = spring_real_x - vx;
		float sdy = spring_real_y - vy;
		float sdist = sqrtf(sdx * sdx + sdy * sdy);
		if (sdist < 1.0f) {
			vx = spring_real_x;
			vy = spring_real_y;
			spring_active = 0;
		}
		/* Let normal lerp handle the correction — target_x/target_y
		 * already equals the real position, so lerp pulls us back */
	}
```

**Step 2: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. On hint jump, cursor overshoots slightly then springs back.

**Step 3: Commit**

```bash
git add src/normal.c
git commit -m "feat: spring overshoot on teleport (8% past destination, lerp spring-back)"
```

---

## Task 5: Acceleration speed lines (Group A, item 3)

When the accelerator key is held, draw 2-3 fading lines trailing behind the cursor.

**Files:**
- Modify: `src/normal.c` — track `accelerating` state, draw speed lines in `redraw()`

**Step 1: Track accelerating state**

In `src/normal.c`, add to `redraw()` signature: `int accelerating`

Update `move()` to accept and pass `accelerating`.

Track whether accelerator is held: the existing code calls `mouse_fast()` on accelerator press and `mouse_normal()` on release. Add a file-static:
```c
static int is_accelerating = 0;
```

Set it in the accelerator handler (line ~386-389):
```c
	} else if (config_input_match(ev, "accelerator")) {
		if (ev->pressed) {
			mouse_fast();
			is_accelerating = 1;
		} else {
			mouse_normal();
			is_accelerating = 0;
		}
```

**Step 2: Draw speed lines in redraw()**

After the comet trail drawing, before the cursor draw:

```c
	/* Speed lines when accelerating */
	if (!hide_cursor && is_accelerating && cur_velocity > 10.0f &&
	    platform->screen_draw_box) {
		float speed = cur_velocity;
		float dir_x = vel_x, dir_y = vel_y;
		float dir_mag = sqrtf(dir_x * dir_x + dir_y * dir_y);
		if (dir_mag > 1.0f) {
			dir_x /= dir_mag;
			dir_y /= dir_mag;
			/* Perpendicular for line spread */
			float perp_x = -dir_y, perp_y = dir_x;

			int k;
			for (k = 0; k < 3; k++) {
				float offset = (k - 1) * cursz * 0.8f;
				float line_len = cursz * 1.5f + speed * 0.02f;
				int lx = draw_x - (int)(dir_x * line_len) + (int)(perp_x * offset);
				int ly = draw_y - (int)(dir_y * line_len) + (int)(perp_y * offset);
				int lw = (int)(line_len);
				int lh = 2;

				int alpha = 80 - k * 20;
				char rgba[16];
				const char *src = curcol;
				if (*src == '#') src++;
				snprintf(rgba, sizeof rgba, "#%.6s%02X", src, alpha);
				platform->screen_draw_box(scr, lx, ly, lw > 0 ? lw : 1, lh, rgba);
			}
		}
	}
```

Note: Speed lines use `screen_draw_box` with thin dimensions for simplicity. Diagonal motion may look slightly off, but this is a v1 — can be refined later.

**Step 3: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Speed lines appear when holding accelerator key and moving.

**Step 4: Commit**

```bash
git add src/normal.c
git commit -m "feat: acceleration speed lines (trailing lines when accelerator held)"
```

---

## Task 6: Idle breathe (Group A, item 9)

After 500ms stationary, deepen the pulse: 3 Hz / 15% amplitude → 1 Hz / 25% amplitude over ~300ms transition.

**Files:**
- Modify: `src/normal.c` — track idle time, adjust `pulse_hz` and pass breathe params
- Modify: `src/platform/macos/macos.m:54-97` — use variable amplitude based on idle state

**Step 1: Track idle time in normal.c**

Add file-statics:
```c
static uint64_t last_movement_ms = 0;
#define IDLE_THRESHOLD_MS 500
#define IDLE_TRANSITION_MS 300
```

In `update_visual_position()`, after lerp:
```c
	if (dist > 2.0f)
		last_movement_ms = get_monotonic_ms();
```

In `redraw()`, compute idle-adjusted pulse:
```c
	uint64_t idle_ms = get_monotonic_ms() - last_movement_ms;
	if (idle_ms > IDLE_THRESHOLD_MS && !dragging && !scroll_is_active()) {
		float idle_t = fminf((float)(idle_ms - IDLE_THRESHOLD_MS) / IDLE_TRANSITION_MS, 1.0f);
		/* Blend from 3 Hz / 15% to 1 Hz / 25% */
		pulse_hz = 3.0f - 2.0f * idle_t;
		/* pulse_amplitude handled in macos.m via pulse_hz < 2.0 heuristic */
	}
```

**Step 2: Adjust amplitude in macos_draw_cursor**

In `src/platform/macos/macos.m`, in `macos_draw_cursor`, change the pulse line:
```c
	/* Amplitude increases as pulse_hz decreases (idle breathe) */
	float amplitude = (pulse_hz < 2.0f) ? 0.25f : 0.15f;
	float pulse = 1.0 + amplitude * sin(t * pulse_hz * 2.0 * M_PI);
```

**Step 3: Trigger continuous redraw during idle breathe**

In `normal.c`, update the continuous redraw condition (line ~356-358) to include idle breathe:
```c
	uint64_t idle_now = get_monotonic_ms() - last_movement_ms;
	if (click_fx_active || mode_flash_active ||
	    cur_velocity > 1.0f ||
	    idle_now > IDLE_THRESHOLD_MS ||
	    fabsf(vx - (float)mx) > 0.5f || fabsf(vy - (float)my) > 0.5f)
		redraw(scr, mx, my, !show_cursor, dragging);
```

**Step 4: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. After 500ms idle, pulse slows from 3Hz to 1Hz with deeper amplitude.

**Step 5: Commit**

```bash
git add src/normal.c src/platform/macos/macos.m
git commit -m "feat: idle breathe (deeper pulse after 500ms stationary)"
```

---

## Task 7: Screen edge pulse (Group B, item 5)

When cursor is within 2px of screen edge, draw a fading line along that edge.

**Files:**
- Modify: `src/normal.c` — detect edge proximity in `redraw()`, draw edge glow

**Step 1: Add edge detection and glow drawing**

In `redraw()`, after the cursor draw call, before the click effect:

```c
	/* Screen edge pulse */
	if (!hide_cursor) {
		int edge_thresh = 2;
		char edge_rgba[16];
		const char *esrc = curcol;
		if (*esrc == '#') esrc++;
		snprintf(edge_rgba, sizeof edge_rgba, "#%.6s40", esrc);  /* 25% alpha */

		int edge_thick = 3;
		if (draw_x <= edge_thresh)
			platform->screen_draw_box(scr, 0, 0, edge_thick, sh, edge_rgba);
		if (draw_x >= sw - edge_thresh)
			platform->screen_draw_box(scr, sw - edge_thick, 0, edge_thick, sh, edge_rgba);
		if (draw_y <= edge_thresh)
			platform->screen_draw_box(scr, 0, 0, sw, edge_thick, edge_rgba);
		if (draw_y >= sh - edge_thresh)
			platform->screen_draw_box(scr, 0, sh - edge_thick, sw, edge_thick, edge_rgba);
	}
```

**Step 2: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. When cursor reaches screen edge, a subtle glow line appears along that edge.

**Step 3: Commit**

```bash
git add src/normal.c
git commit -m "feat: screen edge pulse (glow line at screen edges)"
```

---

## Task 8: Hint cascade wave (Group C, item 6)

Hints ripple outward from cursor position instead of all appearing at once. Each hint gets a delay based on its distance from cursor.

**Files:**
- Modify: `src/platform/macos/hint.m:22-81` — add per-hint delay based on distance from cursor
- Modify: `src/platform/macos/hint.m:83-92` — store cursor position at draw time

**Step 1: Store cursor position at hint draw time**

In `src/platform/macos/hint.m`, add statics:
```c
static int cursor_x_at_draw, cursor_y_at_draw;
```

In `osx_hint_draw()`, before recording `hint_appear_time`, get cursor position:
```c
void osx_hint_draw(struct screen *scr, struct hint *hints, size_t n)
{
	scr->nr_hints = n;
	memcpy(scr->hints, hints, sizeof(struct hint)*n);

	/* Get cursor position for cascade wave */
	extern struct platform *platform;
	int cx, cy;
	platform->mouse_get_position(NULL, &cx, &cy);
	cursor_x_at_draw = cx;
	cursor_y_at_draw = cy;

	hint_appear_time = hint_monotonic_ms();
	window_register_draw_hook(scr->overlay, draw_hook, scr);
}
```

**Step 2: Apply per-hint delay in draw_hook**

In `draw_hook()`, compute per-hint distance and delay:

Replace the animation progress block with:
```c
	uint64_t base_elapsed = hint_monotonic_ms() - hint_appear_time;

	for (i = 0; i < scr->nr_hints; i++) {
		struct hint *h = &scr->hints[i];

		/* Per-hint delay: distance from cursor × 0.08 ms/px */
		float hcx = h->x + h->w * 0.5f;
		float hcy = h->y + h->h * 0.5f;
		float dist = sqrtf((hcx - cursor_x_at_draw) * (hcx - cursor_x_at_draw) +
				   (hcy - cursor_y_at_draw) * (hcy - cursor_y_at_draw));
		uint64_t delay_ms = (uint64_t)(dist * 0.08f);
		if (delay_ms > 120) delay_ms = 120;  /* cap total spread */

		if (base_elapsed < delay_ms)
			continue;  /* not yet visible */

		uint64_t hint_elapsed = base_elapsed - delay_ms;
		float anim_t = (hint_elapsed >= HINT_ANIM_MS) ? 1.0f :
			       (float)hint_elapsed / (float)HINT_ANIM_MS;
		float ease = 1.0f - (1.0f - anim_t) * (1.0f - anim_t);
		float scale = 0.8f + 0.2f * ease;
		float alpha = ease;
```

Remove the old uniform animation block (the `float anim_t = ...` / `ease` / `scale` / `alpha` lines that are outside the loop).

**Step 3: Update re-draw trigger**

The animation re-trigger condition should account for the total cascade time (HINT_ANIM_MS + 120ms max delay = 200ms):
```c
	/* Request another redraw if animation is still in progress */
	if (base_elapsed < HINT_ANIM_MS + 120) {
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
			       5 * NSEC_PER_MSEC),
			       dispatch_get_main_queue(), ^{
			[view setNeedsDisplay:YES];
		});
	}
```

**Step 4: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Hints cascade outward from cursor position — closer hints appear first.

**Step 5: Commit**

```bash
git add src/platform/macos/hint.m
git commit -m "feat: hint cascade wave (ripple outward from cursor position)"
```

---

## Task 9: History ghost dots (Group C, item 7)

Before showing history hints, draw faint circles at each history position.

**Files:**
- Modify: `src/hint.c:289-317` — in `history_hint_mode()`, draw ghost dots before `hint_selection()`

**Step 1: Draw ghost dots**

In `src/hint.c`, in `history_hint_mode()`, after the hints are built but before `hint_selection()`:

```c
	/* Draw ghost dots at history positions */
	if (platform->screen_draw_circle) {
		for (i = 0; i < n; i++) {
			platform->screen_draw_circle(scr,
				ents[i].x, ents[i].y,
				w / 3, w / 6,
				config_get("cursor_color"));
		}
		platform->commit();
	}

	return hint_selection(scr, hints, n);
```

Note: The ghost dots use cursor_color at default alpha. Since `screen_draw_circle` is a no-op on non-macOS, this naturally degrades. The hint_selection call will clear the screen and redraw with the actual hints on top.

Wait — `hint_selection` calls `filter` which calls `screen_clear` then `hint_draw`, which would clear our ghost dots. We need a different approach: draw ghosts as part of the hint draw. The simplest approach: draw them once before entering hint_selection, and they'll appear for a frame before hints overlay them. Actually, since `hint_selection` immediately calls `filter(scr, "")` which clears and redraws, the ghosts won't be visible.

Better approach: Draw the ghosts inside `hint_selection`'s filter function by having them as extra circles. But that's too invasive.

Simplest approach that works: Draw ghost dots, commit, brief pause, then proceed to hint_selection. The ghost dots serve as a "preview" flash before hints appear.

```c
	/* Flash ghost dots at history positions before showing hints */
	platform->screen_clear(scr);
	if (platform->screen_draw_circle) {
		char ghost_rgba[16];
		const char *gc = config_get("cursor_color");
		if (*gc == '#') gc++;
		snprintf(ghost_rgba, sizeof ghost_rgba, "#%.6s33", gc);  /* 20% alpha */

		for (i = 0; i < n; i++) {
			platform->screen_draw_circle(scr,
				ents[i].x, ents[i].y,
				w / 3, w / 6,
				ghost_rgba);
		}
		platform->commit();

		/* Brief flash visible before hint overlay */
		struct timespec ts = { .tv_sec = 0, .tv_nsec = 150 * 1000000 };
		nanosleep(&ts, NULL);
	}

	return hint_selection(scr, hints, n);
```

**Step 2: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Entering history mode shows brief ghost dots at previous click positions.

**Step 3: Commit**

```bash
git add src/hint.c
git commit -m "feat: history ghost dots (faint position preview before hint overlay)"
```

---

## Task 10: Grid sweep animation (Group C, item 10)

Grid lines sweep inward from edges over ~100ms instead of appearing instantly.

**Files:**
- Modify: `src/grid.c:13-31` — add animation state and clip grid lines from edges

**Step 1: Add grid animation state**

In `src/grid.c`, add:
```c
#include <time.h>

static uint64_t grid_appear_time = 0;
#define GRID_SWEEP_MS 100

static uint64_t grid_monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
```

**Step 2: Record appear time on first draw**

In `grid_mode()`, after `redraw(mx, my, 1)` (line ~102), record:
```c
	grid_appear_time = grid_monotonic_ms();
```

**Step 3: Apply sweep animation in draw_grid**

In `draw_grid()`, add an `anim_t` parameter or compute it inside. The simplest approach: compute animation progress from `grid_appear_time` in `redraw()` and pass as a clip factor.

In `redraw()`, before calling `draw_grid`:
```c
	uint64_t grid_elapsed = grid_monotonic_ms() - grid_appear_time;
	float grid_anim = (grid_elapsed >= GRID_SWEEP_MS) ? 1.0f :
			  (float)grid_elapsed / GRID_SWEEP_MS;
	/* Ease-out */
	grid_anim = 1.0f - (1.0f - grid_anim) * (1.0f - grid_anim);
```

Modify `draw_grid` to accept `float anim_t` and clip visible lines:
```c
static void draw_grid(screen_t scr,
		      const char *color, int sz,
		      int nc, int nr,
		      int x, int y, int w, int h,
		      float anim_t)
{
	int i;

	const int ygap = (h - ((nr+1)*sz))/nr;
	const int xgap = (w - ((nc+1)*sz))/nc;

	if (xgap < 0 || ygap < 0)
		return;

	/* Horizontal lines sweep from top/bottom edges inward */
	int visible_rows = (int)((nr + 1) * anim_t + 0.5f);
	for (i = 0; i < nr+1 && i < visible_rows; i++) {
		/* Draw from edges: alternate top and bottom */
		int row = (i % 2 == 0) ? i / 2 : nr - i / 2;
		if (row >= 0 && row <= nr)
			platform->screen_draw_box(scr, x, y+(ygap+sz)*row, w, sz, color);
	}

	/* Vertical lines sweep from left/right edges inward */
	int visible_cols = (int)((nc + 1) * anim_t + 0.5f);
	for (i = 0; i < nc+1 && i < visible_cols; i++) {
		int col = (i % 2 == 0) ? i / 2 : nc - i / 2;
		if (col >= 0 && col <= nc)
			platform->screen_draw_box(scr, x+(xgap+sz)*col, y, sz, h, color);
	}
}
```

Update both `draw_grid` call sites in `redraw()` to pass `grid_anim`.

**Step 4: Trigger continuous redraw during sweep**

In the grid event loop (line ~126-127 of `grid_mode()`), add a continuous redraw condition:
```c
		/* Timeout */
		if (!ev || !ev->pressed) {
			/* Keep redrawing during grid sweep animation */
			if (grid_monotonic_ms() - grid_appear_time < GRID_SWEEP_MS)
				redraw(mx, my, 1);
			continue;
		}
```

**Step 5: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Grid lines sweep in from edges when grid mode opens.

**Step 6: Commit**

```bash
git add src/grid.c
git commit -m "feat: grid sweep animation (lines sweep from edges inward over 100ms)"
```

---

## Task 11: Adaptive contrast — smart sampling (Group D, item 8)

Use `CGWindowListCreateImage` to sample background luminance around cursor, switching to dark cursor on light backgrounds. macOS-only.

**Files:**
- Modify: `src/platform/macos/macos.h` — add `osx_sample_background_luminance` declaration
- Create: `src/platform/macos/contrast.m` — implements background sampling
- Modify: `src/normal.c` — call luminance sampling and override cursor color
- Modify: `src/config.c` — add `cursor_color_dark` config option
- Modify: `src/platform.h` — add `sample_bg_luminance` function pointer (optional, can be NULL)
- Modify: `mk/macos.mk` — add `contrast.m` to build

**Step 1: Add platform function pointer**

In `src/platform.h`, add after `commit`:
```c
	/* Optional: sample background luminance around cursor (returns 0.0-1.0, -1 if unsupported) */
	float (*sample_bg_luminance)(screen_t scr, int x, int y);
```

**Step 2: Add config option**

In `src/config.c`, add:
```c
{ "cursor_color_dark", "#333333", "Cursor color on light backgrounds.", OPT_STRING },
```

**Step 3: Create contrast.m**

Create `src/platform/macos/contrast.m`:
```objc
#include "macos.h"

float osx_sample_bg_luminance(struct screen *scr, int x, int y)
{
	/* Capture 20x20 pixels around cursor from windows below ours */
	int sample_size = 20;
	int sx = scr->x + x - sample_size / 2;
	int sy = scr->y + y - sample_size / 2;

	/* Convert ULO to CG screen coordinates */
	CGRect captureRect = CGRectMake(sx, sy, sample_size, sample_size);

	CGImageRef image = CGWindowListCreateImage(
		captureRect,
		kCGWindowListOptionOnScreenBelowWindow,
		(CGWindowID)[scr->overlay->win windowNumber],
		kCGWindowImageBestResolution);

	if (!image)
		return -1.0f;

	/* Get bitmap data */
	CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(image));
	if (!data) {
		CGImageRelease(image);
		return -1.0f;
	}

	const uint8_t *pixels = CFDataGetBytePtr(data);
	size_t bpp = CGImageGetBitsPerPixel(image) / 8;
	size_t width = CGImageGetWidth(image);
	size_t height = CGImageGetHeight(image);
	size_t stride = CGImageGetBytesPerRow(image);

	/* Compute average luminance (BT.709) */
	float total_lum = 0.0f;
	size_t count = 0;
	for (size_t row = 0; row < height; row++) {
		for (size_t col = 0; col < width; col++) {
			const uint8_t *px = pixels + row * stride + col * bpp;
			/* CGImage is typically BGRA or RGBA */
			float r = px[1] / 255.0f;
			float g = px[2] / 255.0f;
			float b = px[3] / 255.0f;
			total_lum += 0.2126f * r + 0.7152f * g + 0.0722f * b;
			count++;
		}
	}

	CFRelease(data);
	CGImageRelease(image);

	return (count > 0) ? total_lum / count : 0.5f;
}
```

Note: The pixel byte order from `CGWindowListCreateImage` may vary. The exact RGB channel offsets depend on the image format. A practical approach: just average all channels with equal weight (`(px[0]+px[1]+px[2])/(3*255.0)`) as a luminance proxy. This is good enough for dark/light classification.

**Step 4: Wire into platform table**

In `src/platform/macos/macos.m`, in the `platform` struct initialization in `mainloop()`:
```c
		.sample_bg_luminance = osx_sample_bg_luminance,
```

Add declaration in `src/platform/macos/macos.h`:
```c
float osx_sample_bg_luminance(struct screen *scr, int x, int y);
```

**Step 5: Wire NULL in other platforms**

In `src/platform/windows/windows.c`, `src/platform/linux/linux.c` — set `.sample_bg_luminance = NULL`.

**Step 6: Use in normal.c**

In `src/normal.c`, add statics for cached luminance:
```c
static float cached_luminance = 0.0f;
static uint64_t last_luminance_sample = 0;
static int last_luminance_x = -100, last_luminance_y = -100;
#define LUMINANCE_INTERVAL_MS 200
#define LUMINANCE_MOVE_THRESH 30
```

In `redraw()`, before color selection:
```c
	/* Adaptive contrast: sample background luminance */
	if (platform->sample_bg_luminance) {
		uint64_t now = get_monotonic_ms();
		int dx_l = draw_x - last_luminance_x;
		int dy_l = draw_y - last_luminance_y;
		int moved = (dx_l * dx_l + dy_l * dy_l) > LUMINANCE_MOVE_THRESH * LUMINANCE_MOVE_THRESH;

		if (moved || (now - last_luminance_sample > LUMINANCE_INTERVAL_MS)) {
			cached_luminance = platform->sample_bg_luminance(scr, draw_x, draw_y);
			last_luminance_sample = now;
			last_luminance_x = draw_x;
			last_luminance_y = draw_y;
		}
	}
```

Then modify the color selection:
```c
	/* Color priority: scroll > drag > normal (with luminance override) */
	const char *curcol;
	float pulse_hz = 3.0;
	if (scroll_is_active()) {
		curcol = config_get("scroll_cursor_color");
	} else if (dragging) {
		curcol = config_get("drag_cursor_color");
		pulse_hz = 5.0;
	} else if (cached_luminance > 0.6f && platform->sample_bg_luminance) {
		curcol = config_get("cursor_color_dark");
	} else {
		curcol = config_get("cursor_color");
	}
```

**Step 7: Update Makefile**

In `mk/macos.mk`, add `src/platform/macos/contrast.m` to the source list (find the `SRCS` or object list variable and add it).

**Step 8: Build and verify**

Run: `cd ~/programs/warpd && make clean && make`
Expected: 0 errors. Cursor switches to dark color on light backgrounds.

**Step 9: Commit**

```bash
git add src/platform.h src/config.c src/platform/macos/contrast.m src/platform/macos/macos.h src/platform/macos/macos.m src/normal.c mk/macos.mk src/platform/windows/windows.c src/platform/linux/linux.c
git commit -m "feat: adaptive contrast (sample background luminance, switch cursor color)"
```

---

## Task 12: Update config files

Add new config options and pywal template entries.

**Files:**
- Modify: `~/.config/warpd/config`
- Modify: `~/.config/wal/templates/warpd-config`

**Step 1: Add to config**

Append to `~/.config/warpd/config`:
```
cursor_color_dark: #333333
```

**Step 2: Add to pywal template**

Append to `~/.config/wal/templates/warpd-config`:
```
cursor_color_dark: #333333
```

**Step 3: Commit**

No git commit for user config files — these are outside the repo.

---

## Task 13: Final build + deploy verification

**Step 1: Clean build**

```bash
cd ~/programs/warpd && make clean && make
```
Expected: 0 errors, 0 warnings (or only pre-existing warnings)

**Step 2: Verify binary name**

```bash
ls -la bin/
```
Expected: `flick` binary (or `warpd` if mk target wasn't updated — check and fix)

**Step 3: Pause for manual deploy**

Ask user to run:
```bash
sudo cp ~/programs/warpd/bin/flick /usr/local/bin/flick && killall warpd; flick
```

**Step 4: Final commit if any fixups needed**

---

## Verification Checklist

1. Binary is named `flick`, prints `flick vX.Y.Z` on `--version`
2. Lockfile uses `flick_` prefix
3. Cursor stretches into oval during fast movement
4. Cursor overshoots and springs back on hint jump
5. Speed lines appear when holding accelerator key
6. Pulse slows to 1Hz after 500ms idle
7. Glow line appears when cursor hits screen edge
8. Hints cascade outward from cursor position
9. Ghost dots flash before history hint mode
10. Grid lines sweep from edges
11. Cursor switches to dark color on light backgrounds
12. All existing features (comet trail, click fx, drag ring, scroll color) still work
