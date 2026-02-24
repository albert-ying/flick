/*
 * warpd - A modal keyboard-driven pointing system.
 *
 * © 2019 Raheman Vaiya (see: LICENSE).
 */

#include "macos.h"

struct screen screens[32];
size_t nr_screens;

static void draw_hook(void *arg, NSView *view)
{
	struct box *b = arg;
	macos_draw_box(b->scr, b->color, b->x, b->y, b->w, b->h, 0);
}

void osx_screen_draw_box(struct screen *scr, int x, int y, int w, int h, const char *color)
{
	assert(scr->nr_boxes < MAX_BOXES);
	struct box *b = &scr->boxes[scr->nr_boxes++];

	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
	b->scr = scr;
	b->color = nscolor_from_hex(color);

	window_register_draw_hook(scr->overlay, draw_hook, b);
}

static void cursor_draw_hook(void *arg, NSView *view)
{
	struct cursor_draw_data *c = arg;
	macos_draw_cursor(c->scr, c->fill_color, c->border_color,
			  c->x, c->y, c->size, c->border_size, c->pulse_hz);
}

void osx_screen_draw_cursor(struct screen *scr, int x, int y, int size,
			    const char *fill_color, const char *border_color,
			    int border_size, float pulse_hz)
{
	struct cursor_draw_data *c = &scr->cursor;

	c->x = x;
	c->y = y;
	c->size = size;
	c->border_size = border_size;
	c->pulse_hz = pulse_hz;
	c->scr = scr;
	c->fill_color = nscolor_from_hex(fill_color);
	c->border_color = nscolor_from_hex(border_color);

	window_register_draw_hook(scr->overlay, cursor_draw_hook, c);
}

static void circle_draw_hook(void *arg, NSView *view)
{
	struct circle_draw_data *c = arg;
	macos_draw_circle(c->scr, c->color, c->cx, c->cy, c->radius, c->thickness);
}

void osx_screen_draw_circle(struct screen *scr, int cx, int cy, int radius,
			    int thickness, const char *color)
{
	assert(scr->nr_circles < MAX_CIRCLES);
	struct circle_draw_data *c = &scr->circles[scr->nr_circles++];

	c->cx = cx;
	c->cy = cy;
	c->radius = radius;
	c->thickness = thickness;
	c->scr = scr;
	c->color = nscolor_from_hex(color);

	window_register_draw_hook(scr->overlay, circle_draw_hook, c);
}

void osx_screen_list(struct screen *rscreens[MAX_SCREENS], size_t *n)
{
	size_t i;

	for (i = 0; i < nr_screens; i++)
		rscreens[i] = &screens[i];

	*n = nr_screens;
}

void osx_screen_clear(struct screen *scr)
{
	scr->nr_boxes = 0;
	scr->nr_circles = 0;
	scr->overlay->nr_hooks = 0;
}

void osx_screen_get_dimensions(struct screen *scr, int *w, int *h)
{
	*w = scr->w;
	*h = scr->h;
}

void macos_init_screen()
{
	for (NSScreen *screen in NSScreen.screens) {
		struct screen *scr = &screens[nr_screens++];

		scr->x = screen.frame.origin.x;
		scr->y = screen.frame.origin.y;
		scr->w = screen.frame.size.width;
		scr->h = screen.frame.size.height;

		scr->overlay = create_overlay_window(scr->x, scr->y, scr->w, scr->h);
	}
}
