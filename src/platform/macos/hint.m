#include "macos.h"
#include <math.h>
#include <time.h>

static float border_radius;

static NSColor *bgColor;
static NSColor *fgColor;
static NSColor *borderColor;
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

	/* Compute animation progress (0.0 → 1.0 over HINT_ANIM_MS) */
	uint64_t elapsed = hint_monotonic_ms() - hint_appear_time;
	float anim_t = (elapsed >= HINT_ANIM_MS) ? 1.0f :
		       (float)elapsed / (float)HINT_ANIM_MS;
	/* Ease-out: 1 - (1-t)^2 */
	float ease = 1.0f - (1.0f - anim_t) * (1.0f - anim_t);
	float scale = 0.8f + 0.2f * ease;
	float alpha = ease;

	for (i = 0; i < scr->nr_hints; i++) {
		struct hint *h = &scr->hints[i];

		/* Scaled dimensions centered on the hint midpoint */
		float cw = h->w * scale;
		float ch = h->h * scale;
		float cx = h->x + (h->w - cw) * 0.5f;
		float cy = h->y + (h->h - ch) * 0.5f;

		/* Convert to LLO */
		float lx = cx;
		float ly = scr->h - cy - ch;

		NSBezierPath *path = [NSBezierPath
		    bezierPathWithRoundedRect:NSMakeRect(lx, ly, cw, ch)
				      xRadius:border_radius * scale
				      yRadius:border_radius * scale];

		/* Fill with animated alpha */
		[[bgColor colorWithAlphaComponent:
			[bgColor alphaComponent] * alpha] setFill];
		[path fill];

		/* Border stroke */
		[[borderColor colorWithAlphaComponent:
			[borderColor alphaComponent] * alpha] setStroke];
		[path setLineWidth:1.0];
		[path stroke];

		/* Text with animated alpha */
		NSColor *animFg = [fgColor colorWithAlphaComponent:alpha];
		macos_draw_text(scr, animFg, font,
				(int)(cx + 0.5f), (int)(cy + 0.5f),
				(int)(cw + 0.5f), (int)(ch + 0.5f),
				h->label);
	}

	/* Request another redraw if animation is still in progress */
	if (anim_t < 1.0f) {
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

	/* Record appear time for animation on first draw */
	hint_appear_time = hint_monotonic_ms();

	window_register_draw_hook(scr->overlay, draw_hook, scr);
}

void osx_init_hint(const char *bg, const char *fg, int _border_radius,
	       const char *font_family)
{
	bgColor = nscolor_from_hex(bg);
	fgColor = nscolor_from_hex(fg);
	borderColor = [fgColor colorWithAlphaComponent:0.25];

	border_radius = (float)_border_radius;
	font = font_family;
}

