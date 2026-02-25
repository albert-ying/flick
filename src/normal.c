/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "warpd.h"
#include <math.h>

/* Click effect animation state */
static int click_fx_active = 0;
static uint64_t click_fx_start = 0;
static int click_fx_x, click_fx_y;

#define CLICK_FX_DURATION_MS 300

/* Mode transition flash state */
static int mode_flash_active = 0;
static uint64_t mode_flash_start = 0;
static int mode_flash_x, mode_flash_y;

#define MODE_FLASH_DURATION_MS 200

/* Smooth interpolation state */
static float vx, vy;          /* visual cursor position (float for subpixel) */
static int interp_initialized = 0;

#define LERP_FACTOR 0.25       /* per-tick blend: ~100ms to settle at 10ms ticks */

/* Comet trail ring buffer */
#define TRAIL_LEN 5
static float trail_x[TRAIL_LEN];
static float trail_y[TRAIL_LEN];
static int trail_head = 0;
static int trail_count = 0;
static uint64_t last_trail_record = 0;

#define TRAIL_RECORD_INTERVAL_MS 15  /* record a trail dot every 15ms */

/* Velocity tracking */
static float cur_velocity = 0.0;  /* pixels per second */
static float vel_x = 0.0f, vel_y = 0.0f;

/* Spring overshoot state */
static int spring_active = 0;
static float spring_real_x, spring_real_y;

/* Acceleration speed lines */
static int is_accelerating = 0;

/* Adaptive contrast */
static float cached_luminance = 0.0f;
static uint64_t last_luminance_sample = 0;
static int last_luminance_x = -100, last_luminance_y = -100;
#define LUMINANCE_INTERVAL_MS 200
#define LUMINANCE_MOVE_THRESH 30

static uint64_t get_monotonic_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void start_mode_flash(int x, int y)
{
	mode_flash_active = 1;
	mode_flash_start = get_monotonic_ms();
	mode_flash_x = x;
	mode_flash_y = y;
}

static void trail_reset(void)
{
	trail_head = 0;
	trail_count = 0;
	last_trail_record = 0;
}

static void trail_record(float x, float y)
{
	uint64_t now = get_monotonic_ms();
	if (now - last_trail_record < TRAIL_RECORD_INTERVAL_MS)
		return;
	last_trail_record = now;

	trail_x[trail_head] = x;
	trail_y[trail_head] = y;
	trail_head = (trail_head + 1) % TRAIL_LEN;
	if (trail_count < TRAIL_LEN)
		trail_count++;
}

static void update_visual_position(int target_x, int target_y)
{
	if (!interp_initialized) {
		vx = (float)target_x;
		vy = (float)target_y;
		interp_initialized = 1;
		return;
	}

	float dx = (float)target_x - vx;
	float dy = (float)target_y - vy;
	float dist = sqrtf(dx * dx + dy * dy);

	if (dist < 0.5f) {
		vx = (float)target_x;
		vy = (float)target_y;
		cur_velocity = 0.0f;
		vel_x = 0.0f;
		vel_y = 0.0f;
		return;
	}

	/* Teleport: hint jump, screen edge, etc. — overshoot 8% */
	if (dist > 300.0f) {
		float dir_x = dx / dist;
		float dir_y = dy / dist;
		float overshoot = dist * 0.08f;

		vx = (float)target_x + dir_x * overshoot;
		vy = (float)target_y + dir_y * overshoot;
		cur_velocity = 0.0f;
		vel_x = 0.0f;
		vel_y = 0.0f;
		trail_reset();
		spring_active = 1;
		spring_real_x = (float)target_x;
		spring_real_y = (float)target_y;
		return;
	}

	/* Spring correction: clear flag when close enough */
	if (spring_active) {
		float sdx = spring_real_x - vx;
		float sdy = spring_real_y - vy;
		if (sqrtf(sdx * sdx + sdy * sdy) < 1.0f) {
			spring_active = 0;
		}
	}

	vx += dx * LERP_FACTOR;
	vy += dy * LERP_FACTOR;

	vel_x = dx * LERP_FACTOR * 100.0f;
	vel_y = dy * LERP_FACTOR * 100.0f;

	/* Velocity in pixels/second (10ms tick → ×100) */
	float step = sqrtf((dx * LERP_FACTOR) * (dx * LERP_FACTOR) +
			   (dy * LERP_FACTOR) * (dy * LERP_FACTOR));
	cur_velocity = cur_velocity * 0.7f + step * 100.0f * 0.3f; /* smoothed */

	trail_record(vx, vy);
}

static void redraw(screen_t scr, int x, int y, int hide_cursor, int dragging)
{
	int sw, sh;

	platform->screen_get_dimensions(scr, &sw, &sh);

	const int gap = 10;
	const int indicator_size = (config_get_int("indicator_size") * sh) / 1080;
	const char *indicator_color = config_get("indicator_color");
	const char *curborder = config_get("cursor_border_color");
	const int curbordersz = config_get_int("cursor_border_size");
	const char *indicator = config_get("indicator");
	const int cursz = config_get_int("cursor_size");

	/* Update smooth visual position */
	update_visual_position(x, y);
	int draw_x = (int)(vx + 0.5f);
	int draw_y = (int)(vy + 0.5f);

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

	/* Color priority: scroll > drag > adaptive contrast > normal */
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

	platform->screen_clear(scr);

	/* Draw comet trail (oldest to newest, behind cursor) */
	if (!hide_cursor && trail_count > 0 && platform->screen_draw_circle) {
		const char *base = curcol;
		char trail_rgba[16];
		const char *src = base;
		if (*src == '#') src++;

		int i;
		for (i = 0; i < trail_count; i++) {
			/* Walk from oldest to newest */
			int idx = (trail_head - trail_count + i + TRAIL_LEN) % TRAIL_LEN;
			float age = (float)(trail_count - 1 - i) / (float)trail_count;

			/* Skip trail dots too close to cursor */
			float tdx = trail_x[idx] - vx;
			float tdy = trail_y[idx] - vy;
			if (sqrtf(tdx * tdx + tdy * tdy) < cursz * 0.4f)
				continue;

			int alpha = (int)((1.0f - age) * 0.35f * 255);
			float radius = cursz * 0.25f * (1.0f - age * 0.6f);
			if (alpha < 5 || radius < 1.0f)
				continue;

			snprintf(trail_rgba, sizeof trail_rgba, "#%.6s%02X", src, alpha);
			platform->screen_draw_circle(scr,
				(int)(trail_x[idx] + 0.5f),
				(int)(trail_y[idx] + 0.5f),
				(int)(radius + 0.5f), (int)(radius + 0.5f),
				trail_rgba);
		}
	}

	/* Speed lines when accelerating */
	if (!hide_cursor && is_accelerating && cur_velocity > 10.0f) {
		float dir_x = vel_x, dir_y = vel_y;
		float dir_mag = sqrtf(dir_x * dir_x + dir_y * dir_y);
		if (dir_mag > 1.0f) {
			dir_x /= dir_mag;
			dir_y /= dir_mag;
			float perp_x = -dir_y, perp_y = dir_x;

			int k;
			for (k = 0; k < 3; k++) {
				float offset = (k - 1) * cursz * 0.8f;
				float line_len = cursz * 1.5f + cur_velocity * 0.02f;
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

	if (!hide_cursor)
		platform->screen_draw_cursor(scr, draw_x, draw_y,
				cursz, curcol, curborder, curbordersz, pulse_hz,
				cur_velocity, vel_x, vel_y);

	/* Screen edge pulse */
	if (!hide_cursor) {
		int edge_thresh = 2;
		char edge_rgba[16];
		const char *esrc = curcol;
		if (*esrc == '#') esrc++;
		snprintf(edge_rgba, sizeof edge_rgba, "#%.6s40", esrc);

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

	/* Sentinel ring while dragging */
	if (dragging && !hide_cursor && platform->screen_draw_circle) {
		platform->screen_draw_circle(scr, draw_x, draw_y,
			cursz * 2, 1, config_get("drag_indicator_color"));
	}

	/* Draw click effect expanding ring */
	if (click_fx_active && platform->screen_draw_circle) {
		uint64_t now = get_monotonic_ms();
		uint64_t elapsed = now - click_fx_start;

		if (elapsed < CLICK_FX_DURATION_MS) {
			double t = (double)elapsed / CLICK_FX_DURATION_MS;
			int radius = cursz + (int)(t * cursz * 4);
			int alpha = (int)((1.0 - t) * 0.8 * 255);
			if (alpha < 0) alpha = 0;
			if (alpha > 255) alpha = 255;

			const char *base_color = config_get("click_effect_color");
			char rgba[16];
			const char *src = base_color;
			if (*src == '#') src++;
			snprintf(rgba, sizeof rgba, "#%.6s%02X", src, alpha);

			platform->screen_draw_circle(scr,
				click_fx_x, click_fx_y,
				radius, 2, rgba);
		} else {
			click_fx_active = 0;
		}
	}

	/* Gravity wave ripple */
	if (platform->draw_ripple)
		platform->draw_ripple(scr);

	/* Mode transition flash */
	if (mode_flash_active && platform->screen_draw_circle) {
		uint64_t now = get_monotonic_ms();
		uint64_t elapsed = now - mode_flash_start;

		if (elapsed < MODE_FLASH_DURATION_MS) {
			double t = (double)elapsed / MODE_FLASH_DURATION_MS;
			int radius = cursz + (int)(t * cursz * 3);
			int alpha = (int)((1.0 - t) * 0.6 * 255);
			if (alpha < 0) alpha = 0;

			char rgba[16];
			const char *src = curcol;
			if (*src == '#') src++;
			snprintf(rgba, sizeof rgba, "#%.6s%02X", src, alpha);

			platform->screen_draw_circle(scr,
				mode_flash_x, mode_flash_y,
				radius, 2, rgba);
		} else {
			mode_flash_active = 0;
		}
	}

	if (!strcmp(indicator, "bottomleft"))
		platform->screen_draw_box(scr, gap, sh-indicator_size-gap, indicator_size, indicator_size, indicator_color);
	else if (!strcmp(indicator, "topleft"))
		platform->screen_draw_box(scr, gap, gap, indicator_size, indicator_size, indicator_color);
	else if (!strcmp(indicator, "topright"))
		platform->screen_draw_box(scr, sw-indicator_size-gap, gap, indicator_size, indicator_size, indicator_color);
	else if (!strcmp(indicator, "bottomright"))
		platform->screen_draw_box(scr, sw-indicator_size-gap, sh-indicator_size-gap, indicator_size, indicator_size, indicator_color);

	platform->commit();
}

static void move(screen_t scr, int x, int y, int hide_cursor, int dragging)
{
	platform->mouse_move(scr, x, y);
	redraw(scr, x, y, hide_cursor, dragging);
}

static void start_click_fx(screen_t scr, int x, int y)
{
	click_fx_active = 1;
	click_fx_start = get_monotonic_ms();
	click_fx_x = x;
	click_fx_y = y;

	/* Trigger gravity wave ripple (macOS-only, NULL on other platforms) */
	if (platform->start_ripple)
		platform->start_ripple(scr, x, y);
}

struct input_event *normal_mode(struct input_event *start_ev, int oneshot)
{
	const int cursz = config_get_int("cursor_size");
	const int system_cursor = config_get_int("normal_system_cursor");
	const char *blink_interval = config_get("normal_blink_interval");

	int on_time, off_time;
	struct input_event *ev;
	screen_t scr;
	int sh, sw;
	int mx, my;
	int dragging = 0;
	int show_cursor = !system_cursor;

	int n = sscanf(blink_interval, "%d %d", &on_time, &off_time);
	assert(n > 0);
	if (n == 1)
		off_time = on_time;

	const char *keys[] = {
		"accelerator",
		"bottom",
		"buttons",
		"copy_and_exit",
		"decelerator",
		"down",
		"drag",
		"end",
		"exit",
		"grid",
		"hint",
		"hint2",
		"hist_back",
		"hist_forward",
		"history",
		"left",
		"middle",
		"oneshot_buttons",
		"print",
		"right",
		"screen",
		"scroll_down",
		"scroll_up",
		"start",
		"top",
		"up",
	};

	platform->input_grab_keyboard();

	platform->mouse_get_position(&scr, &mx, &my);
	platform->screen_get_dimensions(scr, &sw, &sh);

	if (!system_cursor)
		platform->mouse_hide();

	mouse_reset();
	click_fx_active = 0;
	interp_initialized = 0;
	cur_velocity = 0.0f;
	trail_reset();
	spring_active = 0;
	is_accelerating = 0;
	start_mode_flash(mx, my);
	redraw(scr, mx, my, !show_cursor, dragging);

	uint64_t time = 0;
	uint64_t last_blink_update = 0;
	while (1) {
		config_input_whitelist(keys, sizeof keys / sizeof keys[0]);
		if (start_ev == NULL) {
			ev = platform->input_next_event(10);
			time += 10;
		} else {
			ev = start_ev;
			start_ev = NULL;
		}

		platform->mouse_get_position(&scr, &mx, &my);

		if (!system_cursor && on_time) {
			if (show_cursor && (time - last_blink_update) >= on_time) {
				show_cursor = 0;
				redraw(scr, mx, my, !show_cursor, dragging);
				last_blink_update = time;
			} else if (!show_cursor && (time - last_blink_update) >= off_time) {
				show_cursor = 1;
				redraw(scr, mx, my, !show_cursor, dragging);
				last_blink_update = time;
			}
		}

		/* Continuous redraw for active animations and smooth interpolation */
		if (click_fx_active || mode_flash_active ||
		    cur_velocity > 5.0f ||
		    (platform->ripple_is_active && platform->ripple_is_active()) ||
		    fabsf(vx - (float)mx) > 0.5f || fabsf(vy - (float)my) > 0.5f)
			redraw(scr, mx, my, !show_cursor, dragging);

		scroll_tick();
		if (mouse_process_key(ev, "up", "down", "left", "right")) {
			redraw(scr, mx, my, !show_cursor, dragging);
			continue;
		}

		if (!ev)  {
			continue;
		} else if (config_input_match(ev, "scroll_down")) {
			redraw(scr, mx, my, !show_cursor, dragging);

			if (ev->pressed) {
				scroll_stop();
				scroll_accelerate(SCROLL_DOWN);
			} else
				scroll_decelerate();
		} else if (config_input_match(ev, "scroll_up")) {
			redraw(scr, mx, my, !show_cursor, dragging);

			if (ev->pressed) {
				scroll_stop();
				scroll_accelerate(SCROLL_UP);
			} else
				scroll_decelerate();
		} else if (config_input_match(ev, "accelerator")) {
			if (ev->pressed) {
				mouse_fast();
				is_accelerating = 1;
			} else {
				mouse_normal();
				is_accelerating = 0;
			}
		} else if (config_input_match(ev, "decelerator")) {
			if (ev->pressed)
				mouse_slow();
			else
				mouse_normal();
		} else if (!ev->pressed) {
			goto next;
		}

		if (config_input_match(ev, "top"))
			move(scr, mx, cursz / 2, !show_cursor, dragging);
		else if (config_input_match(ev, "bottom"))
			move(scr, mx, sh - cursz / 2, !show_cursor, dragging);
		else if (config_input_match(ev, "middle"))
			move(scr, mx, sh / 2, !show_cursor, dragging);
		else if (config_input_match(ev, "start"))
			move(scr, 1, my, !show_cursor, dragging);
		else if (config_input_match(ev, "end"))
			move(scr, sw - cursz, my, !show_cursor, dragging);
		else if (config_input_match(ev, "hist_back")) {
			hist_add(mx, my);
			hist_prev();
			hist_get(&mx, &my);

			move(scr, mx, my, !show_cursor, dragging);
		} else if (config_input_match(ev, "hist_forward")) {
			hist_next();
			hist_get(&mx, &my);

			move(scr, mx, my, !show_cursor, dragging);
		} else if (config_input_match(ev, "drag")) {
			dragging = !dragging;
			start_mode_flash(mx, my);
			if (dragging)
				platform->mouse_down(config_get_int("drag_button"));
			else
				platform->mouse_up(config_get_int("drag_button"));
		} else if (config_input_match(ev, "copy_and_exit")) {
			platform->mouse_up(config_get_int("drag_button"));
			platform->copy_selection();
			ev = NULL;
			goto exit;
		} else if (config_input_match(ev, "exit") ||
			   config_input_match(ev, "grid") ||
			   config_input_match(ev, "screen") ||
			   config_input_match(ev, "history") ||
			   config_input_match(ev, "hint2") ||
			   config_input_match(ev, "hint")) {
			goto exit;
		} else if (config_input_match(ev, "print")) {
			printf("%d %d %s\n", mx, my, input_event_tostr(ev));
			fflush(stdout);
		} else { /* Mouse Buttons. */
			int btn;

			if ((btn = config_input_match(ev, "buttons"))) {
				if (oneshot) {
					printf("%d %d\n", mx, my);
					exit(btn);
				}

				hist_add(mx, my);
				histfile_add(mx, my);
				platform->mouse_click(btn);
				start_click_fx(scr, mx, my);
			} else if ((btn = config_input_match(ev, "oneshot_buttons"))) {
				hist_add(mx, my);
				platform->mouse_click(btn);
				start_click_fx(scr, mx, my);

				const int timeout = config_get_int("oneshot_timeout");

				while (1) {
					struct input_event *ev = platform->input_next_event(timeout);

					if (!ev)
						break;

					if (ev && ev->pressed &&
						config_input_match(ev, "oneshot_buttons")) {
						platform->mouse_click(btn);
					}
				}

				goto exit;
			}
		}
	next:
		platform->mouse_get_position(&scr, &mx, &my);

		platform->commit();
	}

exit:
	platform->mouse_show();
	platform->screen_clear(scr);

	platform->input_ungrab_keyboard();

	platform->commit();
	return ev;
}
