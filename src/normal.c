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

static uint64_t get_monotonic_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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

	/* Color priority: scroll > drag > normal */
	const char *curcol;
	float pulse_hz = 3.0;
	if (scroll_is_active()) {
		curcol = config_get("scroll_cursor_color");
	} else if (dragging) {
		curcol = config_get("drag_cursor_color");
		pulse_hz = 5.0;
	} else {
		curcol = config_get("cursor_color");
	}

	platform->screen_clear(scr);

	if (!hide_cursor)
		platform->screen_draw_cursor(scr, x, y,
				cursz, curcol, curborder, curbordersz, pulse_hz);

	/* Sentinel ring while dragging */
	if (dragging && !hide_cursor && platform->screen_draw_circle) {
		platform->screen_draw_circle(scr, x, y,
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
			/* Build RGBA hex: base color + computed alpha */
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

static void start_click_fx(int x, int y)
{
	click_fx_active = 1;
	click_fx_start = get_monotonic_ms();
	click_fx_x = x;
	click_fx_y = y;
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

		/* Redraw while click effect is active to animate the ring */
		if (click_fx_active)
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
			if (ev->pressed)
				mouse_fast();
			else
				mouse_normal();
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
				start_click_fx(mx, my);
			} else if ((btn = config_input_match(ev, "oneshot_buttons"))) {
				hist_add(mx, my);
				platform->mouse_click(btn);
				start_click_fx(mx, my);

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
