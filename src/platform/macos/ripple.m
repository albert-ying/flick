/*
 * flick - Gravity wave click effect.
 * Captures screen at click time, applies animated radial distortion.
 * macOS-only (uses Core Image CIBumpDistortion).
 */

#include "macos.h"
#import <QuartzCore/QuartzCore.h>

/*
 * CGWindowListCreateImage was obsoleted in macOS 15.0 SDK.
 * Redeclare it under a different name linked to the same linker symbol
 * to bypass the availability attribute.
 */
extern CGImageRef
warpd_CGWindowListCreateImage(CGRect, CGWindowListOption,
	CGWindowID, CGWindowImageOption)
	__asm("_CGWindowListCreateImage");

/* Ripple effect state — one active ripple at a time */
static CGImageRef ripple_capture = NULL;
static CIImage *ripple_ci_image = nil;
static int ripple_cx, ripple_cy;
static uint64_t ripple_start_ms = 0;
static struct screen *ripple_scr = NULL;
static int ripple_active = 0;

#define RIPPLE_DURATION_MS 400
#define RIPPLE_CAPTURE_SIZE 300  /* px radius of capture region */

static uint64_t ripple_monotonic_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void osx_ripple_start(struct screen *scr, int x, int y)
{
	/* Release previous capture if any */
	if (ripple_capture) {
		CGImageRelease(ripple_capture);
		ripple_capture = NULL;
	}
	ripple_ci_image = nil;

	int capture_sz = RIPPLE_CAPTURE_SIZE * 2;
	int sx = scr->x + x - RIPPLE_CAPTURE_SIZE;
	int sy = scr->y + y - RIPPLE_CAPTURE_SIZE;

	CGRect captureRect = CGRectMake(sx, sy, capture_sz, capture_sz);

	ripple_capture = warpd_CGWindowListCreateImage(
		captureRect,
		kCGWindowListOptionOnScreenBelowWindow,
		(CGWindowID)[scr->overlay->win windowNumber],
		kCGWindowImageDefault);

	if (!ripple_capture) {
		ripple_active = 0;
		return;
	}

	ripple_ci_image = [CIImage imageWithCGImage:ripple_capture];
	ripple_cx = x;
	ripple_cy = y;
	ripple_scr = scr;
	ripple_start_ms = ripple_monotonic_ms();
	ripple_active = 1;
}

int osx_ripple_is_active(void)
{
	return ripple_active;
}

/* Draw hook adapter — called from window_register_draw_hook */
static void ripple_draw_hook(void *arg, NSView *view)
{
	(void)view;
	struct screen *scr = arg;
	osx_ripple_draw(scr, view);
}

/* Platform-level wrapper: register ripple drawing as a hook for next commit */
void osx_draw_ripple(struct screen *scr)
{
	if (!ripple_active)
		return;
	window_register_draw_hook(scr->overlay, ripple_draw_hook, scr);
}

void osx_ripple_draw(struct screen *scr, NSView *view)
{
	if (!ripple_active || !ripple_ci_image || scr != ripple_scr)
		return;

	uint64_t elapsed = ripple_monotonic_ms() - ripple_start_ms;
	if (elapsed >= RIPPLE_DURATION_MS) {
		ripple_active = 0;
		if (ripple_capture) {
			CGImageRelease(ripple_capture);
			ripple_capture = NULL;
		}
		ripple_ci_image = nil;
		return;
	}

	float t = (float)elapsed / RIPPLE_DURATION_MS;

	/* Expanding radius, fading distortion scale */
	float radius = RIPPLE_CAPTURE_SIZE * (0.2f + 0.8f * t);
	float scale = 0.6f * (1.0f - t);  /* start strong, fade to 0 */

	/* Center in CI coordinate space (LLO, relative to capture image) */
	CIVector *center = [CIVector vectorWithX:RIPPLE_CAPTURE_SIZE
					       Y:RIPPLE_CAPTURE_SIZE];

	CIFilter *bump = [CIFilter filterWithName:@"CIBumpDistortion"];
	[bump setDefaults];
	[bump setValue:ripple_ci_image forKey:kCIInputImageKey];
	[bump setValue:center forKey:kCIInputCenterKey];
	[bump setValue:@(radius) forKey:kCIInputRadiusKey];
	[bump setValue:@(scale) forKey:kCIInputScaleKey];

	CIImage *output = [bump outputImage];
	if (!output)
		return;

	/* Draw the distorted image at the correct position in the overlay */
	NSGraphicsContext *nsCtx = [NSGraphicsContext currentContext];
	CIContext *ciCtx = [nsCtx CIContext];

	/* Convert overlay ULO position to LLO for drawing */
	float draw_x = ripple_cx - RIPPLE_CAPTURE_SIZE;
	float draw_y = scr->h - ripple_cy - RIPPLE_CAPTURE_SIZE;

	CGRect destRect = CGRectMake(draw_x, draw_y,
				     RIPPLE_CAPTURE_SIZE * 2,
				     RIPPLE_CAPTURE_SIZE * 2);

	/* Crop output to original extent */
	CGRect extent = CGRectMake(0, 0,
				   RIPPLE_CAPTURE_SIZE * 2,
				   RIPPLE_CAPTURE_SIZE * 2);

	[ciCtx drawImage:output inRect:destRect fromRect:extent];
}
