#include "macos.h"
#include <math.h>
#include <time.h>

static float border_radius;

static NSColor *bgColor;
static NSColor *fgColor;
const char *font;

/* Hint appear animation */
static uint64_t hint_appear_time = 0;
static int cursor_x_at_draw, cursor_y_at_draw;
#define HINT_ANIM_MS 80

static uint64_t hint_monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void draw_hook(void *arg, NSView *view)
{
	size_t i;
	struct screen *scr = arg;

	uint64_t base_elapsed = hint_monotonic_ms() - hint_appear_time;

	for (i = 0; i < scr->nr_hints; i++) {
		struct hint *h = &scr->hints[i];

		/* Per-hint delay: distance from cursor × 0.08 ms/px */
		float hcx = h->x + h->w * 0.5f;
		float hcy = h->y + h->h * 0.5f;
		float dist = sqrtf((hcx - cursor_x_at_draw) * (hcx - cursor_x_at_draw) +
				   (hcy - cursor_y_at_draw) * (hcy - cursor_y_at_draw));
		uint64_t delay_ms = (uint64_t)(dist * 0.08f);
		if (delay_ms > 120) delay_ms = 120;

		if (base_elapsed < delay_ms)
			continue;  /* not yet visible */

		uint64_t hint_elapsed = base_elapsed - delay_ms;
		float anim_t = (hint_elapsed >= HINT_ANIM_MS) ? 1.0f :
			       (float)hint_elapsed / (float)HINT_ANIM_MS;
		float ease = 1.0f - (1.0f - anim_t) * (1.0f - anim_t);
		float scale = 0.8f + 0.2f * ease;
		float alpha = ease;

		/* Scaled dimensions centered on the hint midpoint */
		float cw = h->w * scale;
		float ch = h->h * scale;
		float cx = h->x + (h->w - cw) * 0.5f;
		float cy = h->y + (h->h - ch) * 0.5f;

		/* Convert to LLO */
		float lx = cx;
		float ly = scr->h - cy - ch;

		float br = border_radius * scale;
		NSRect hintRect = NSMakeRect(lx, ly, cw, ch);
		NSBezierPath *path = [NSBezierPath
		    bezierPathWithRoundedRect:hintRect
				      xRadius:br
				      yRadius:br];

		/* Layer 1: Translucent fill (~55% alpha) */
		[[bgColor colorWithAlphaComponent:0.55f * alpha] setFill];
		[path fill];

		/* Layer 2: Top-edge specular highlight */
		[NSGraphicsContext saveGraphicsState];
		[path addClip];
		float highlightH = ch * 0.4f;
		NSGradient *highlight = [[NSGradient alloc]
		    initWithStartingColor:
			[[NSColor whiteColor] colorWithAlphaComponent:0.18f * alpha]
		    endingColor:
			[[NSColor whiteColor] colorWithAlphaComponent:0.0f]];
		[highlight drawInRect:NSMakeRect(lx, ly + ch - highlightH, cw, highlightH)
			       angle:270];
		[NSGraphicsContext restoreGraphicsState];

		/* Layer 3: Specular border — bright top, dim bottom */
		NSBezierPath *strokePath = [NSBezierPath
		    bezierPathWithRoundedRect:NSInsetRect(hintRect, 0.5, 0.5)
				      xRadius:br
				      yRadius:br];
		[strokePath setLineWidth:1.0];
		float midY = ly + ch * 0.5f;

		/* Top half: brighter */
		[NSGraphicsContext saveGraphicsState];
		NSRectClip(NSMakeRect(lx, midY, cw, ch));
		[[[NSColor whiteColor] colorWithAlphaComponent:0.30f * alpha] setStroke];
		[strokePath stroke];
		[NSGraphicsContext restoreGraphicsState];

		/* Bottom half: dimmer */
		[NSGraphicsContext saveGraphicsState];
		NSRectClip(NSMakeRect(lx, ly, cw, ch * 0.5f));
		[[[NSColor whiteColor] colorWithAlphaComponent:0.08f * alpha] setStroke];
		[strokePath stroke];
		[NSGraphicsContext restoreGraphicsState];

		/* Text with animated alpha */
		NSColor *animFg = [fgColor colorWithAlphaComponent:alpha];
		macos_draw_text(scr, animFg, font,
				(int)(cx + 0.5f), (int)(cy + 0.5f),
				(int)(cw + 0.5f), (int)(ch + 0.5f),
				h->label);
	}

	/* Request another redraw if cascade animation still in progress */
	if (base_elapsed < HINT_ANIM_MS + 120) {
		dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
			       5 * NSEC_PER_MSEC),
			       dispatch_get_main_queue(), ^{
			[view setNeedsDisplay:YES];
		});
	}
}

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

void osx_init_hint(const char *bg, const char *fg, int _border_radius,
	       const char *font_family)
{
	bgColor = nscolor_from_hex(bg);
	fgColor = nscolor_from_hex(fg);

	border_radius = (float)_border_radius;
	font = font_family;
}

