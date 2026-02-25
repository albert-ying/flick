/*
 * flick - Adaptive contrast sampling.
 * Captures background pixels around cursor and computes average luminance.
 * macOS-only (uses CGWindowListCreateImage).
 */

#include "macos.h"

/*
 * CGWindowListCreateImage was obsoleted in macOS 15.0 SDK.
 * Redeclare it under a different name linked to the same linker symbol
 * to bypass the availability attribute.
 */
extern CGImageRef
warpd_CGWindowListCreateImage(CGRect, CGWindowListOption,
	CGWindowID, CGWindowImageOption)
	__asm("_CGWindowListCreateImage");

float osx_sample_bg_luminance(struct screen *scr, int x, int y)
{
	int sample_size = 20;
	int sx = scr->x + x - sample_size / 2;
	int sy = scr->y + y - sample_size / 2;

	CGRect captureRect = CGRectMake(sx, sy, sample_size, sample_size);

	CGImageRef image = warpd_CGWindowListCreateImage(
		captureRect,
		kCGWindowListOptionOnScreenBelowWindow,
		(CGWindowID)[scr->overlay->win windowNumber],
		kCGWindowImageDefault);

	if (!image)
		return -1.0f;

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

	/* Average luminance — simple channel average as proxy */
	float total_lum = 0.0f;
	size_t count = 0;
	for (size_t row = 0; row < height; row++) {
		for (size_t col = 0; col < width; col++) {
			const uint8_t *px = pixels + row * stride + col * bpp;
			/* BGRA format typical from CGWindowListCreateImage */
			float lum = (px[0] + px[1] + px[2]) / (3.0f * 255.0f);
			total_lum += lum;
			count++;
		}
	}

	CFRelease(data);
	CGImageRelease(image);

	return (count > 0) ? total_lum / count : 0.5f;
}
