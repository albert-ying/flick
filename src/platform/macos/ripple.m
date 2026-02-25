/*
 * flick - Gravity wave click effect.
 * Draws expanding concentric ripple rings at click position.
 * macOS-only.
 */

#include "macos.h"

/* Ripple effect state — one active ripple at a time */
static int ripple_cx, ripple_cy;
static uint64_t ripple_start_ms = 0;
static struct screen *ripple_scr = NULL;
static int ripple_active = 0;
static NSColor *ripple_color = nil;

#define RIPPLE_DURATION_MS 350
#define RIPPLE_MAX_RADIUS 80
#define RIPPLE_NUM_RINGS 3

static uint64_t ripple_monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void osx_ripple_start(struct screen *scr, int x, int y, const char *color)
{
	ripple_cx = x;
	ripple_cy = y;
	ripple_scr = scr;
	ripple_start_ms = ripple_monotonic_ms();
	ripple_active = 1;
	ripple_color = nscolor_from_hex(color);
}

int osx_ripple_is_active(void)
{
	return ripple_active;
}

/* Draw hook — renders expanding rings directly via NSBezierPath */
static void ripple_draw_hook(void *arg, NSView *view)
{
	(void)view;
	struct screen *scr = arg;

	if (!ripple_active || scr != ripple_scr)
		return;

	uint64_t elapsed = ripple_monotonic_ms() - ripple_start_ms;
	if (elapsed >= RIPPLE_DURATION_MS) {
		ripple_active = 0;
		return;
	}

	float t = (float)elapsed / RIPPLE_DURATION_MS;

	/* Convert ULO → LLO */
	float cx = ripple_cx;
	float cy = scr->h - ripple_cy;

	int i;
	for (i = 0; i < RIPPLE_NUM_RINGS; i++) {
		/* Stagger rings: each starts slightly later */
		float ring_delay = (float)i * 0.15f;
		float rt = (t - ring_delay);
		if (rt < 0.0f) continue;
		if (rt > 1.0f) rt = 1.0f;

		float ease = 1.0f - (1.0f - rt) * (1.0f - rt); /* ease-out */
		float radius = RIPPLE_MAX_RADIUS * ease;
		float alpha = 0.35f * (1.0f - rt);
		float thickness = 1.5f * (1.0f - rt * 0.5f);

		if (alpha < 0.02f || radius < 1.0f)
			continue;

		NSRect rect = NSMakeRect(cx - radius, cy - radius,
					 radius * 2, radius * 2);
		NSBezierPath *path = [NSBezierPath bezierPathWithOvalInRect:rect];
		[[ripple_color colorWithAlphaComponent:alpha] setStroke];
		[path setLineWidth:thickness];
		[path stroke];
	}
}

/* Platform-level wrapper: register ripple drawing as a hook for next commit */
void osx_draw_ripple(struct screen *scr)
{
	if (!ripple_active)
		return;
	window_register_draw_hook(scr->overlay, ripple_draw_hook, scr);
}
