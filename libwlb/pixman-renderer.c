/*
 * Copyright © 2013 Jason Ekstrand
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <pixman.h>

#include "wlb-private.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

struct wlb_pixman_renderer {
	pixman_image_t *black_image;
};

WL_EXPORT struct wlb_pixman_renderer *
wlb_pixman_renderer_create(struct wlb_compositor *c)
{
	struct wlb_pixman_renderer *pr;
	pixman_color_t color;

	pr = zalloc(sizeof *pr);
	if (!pr)
		return NULL;

	color.red = 0;
	color.green = 0;
	color.blue = 0;
	color.alpha = 0xffff;

	pr->black_image = pixman_image_create_solid_fill(&color);

	return pr;
}

WL_EXPORT void
wlb_pixman_renderer_destroy(struct wlb_pixman_renderer *pr)
{
	pixman_image_unref(pr->black_image);

	free(pr);
}

static void
fill_with_black(struct wlb_pixman_renderer *pr, pixman_image_t *image,
		pixman_region32_t *region)
{
	pixman_color_t color;
	pixman_image_t *black_image;
	pixman_box32_t *rects;
	int i, nrects;

	if (!pixman_region32_not_empty(region))
		return;

	rects = pixman_region32_rectangles(region, &nrects);
	for (i = 0; i < nrects; ++i)
		pixman_image_composite32(PIXMAN_OP_SRC, pr->black_image, NULL,
					 image, 0, 0, 0, 0,
					 rects[i].x1, rects[i].y1,
					 rects[i].x2 - rects[i].x1,
					 rects[i].y2 - rects[i].y1);
}

static void
paint_shm_buffer(pixman_image_t *image, pixman_region32_t *region,
		 struct wl_shm_buffer *buffer, struct wlb_rectangle *pos)
{
	pixman_format_code_t format;
	int32_t swidth, sheight;
	pixman_image_t *surface_image;
	pixman_fixed_t xscale, yscale;
	pixman_transform_t transform;
	pixman_box32_t *rects;
	int i, nrects;

	switch(wl_shm_buffer_get_format(buffer)) {
	case WL_SHM_FORMAT_XRGB8888:
		format = PIXMAN_x8r8g8b8;
		break;
	case WL_SHM_FORMAT_ARGB8888:
		format = PIXMAN_a8r8g8b8;
		break;
	case WL_SHM_FORMAT_RGB565:
		format = PIXMAN_r5g6b5;
		break;
	default:
		printf("Unsupported SHM buffer format\n");
		return;
	}
	
	swidth = wl_shm_buffer_get_width(buffer);
	sheight = wl_shm_buffer_get_height(buffer);

	surface_image =
		pixman_image_create_bits(format, swidth, sheight,
					 wl_shm_buffer_get_data(buffer),
					 wl_shm_buffer_get_stride(buffer));

	xscale = swidth * pixman_fixed_1 / pos->width;
	yscale = sheight * pixman_fixed_1 / pos->height;
	if (xscale != pixman_fixed_1 || yscale != pixman_fixed_1) {
		pixman_transform_init_scale(&transform, xscale, yscale);
		pixman_image_set_transform(surface_image, &transform);
		pixman_image_set_filter(surface_image,
					PIXMAN_FILTER_BILINEAR, NULL, 0);
	}

	rects = pixman_region32_rectangles(region, &nrects);
	for (i = 0; i < nrects; ++i) {
		pixman_image_composite32(PIXMAN_OP_SRC,
					 surface_image, NULL, image,
					 rects[i].x1 - pos->x, rects[i].y1 - pos->y,
					 0, 0,
					 rects[i].x1, rects[i].y1,
					 rects[i].x2 - rects[i].x1,
					 rects[i].y2 - rects[i].y1);
	}

	pixman_image_unref(surface_image);
}

WL_EXPORT void
wlb_pixman_renderer_repaint_output(struct wlb_pixman_renderer *pr,
				   struct wlb_output *output,
				   pixman_image_t *image)
{
	int32_t width, height;
	pixman_region32_t damage, surface_damage;
	struct wl_shm_buffer *buffer;
	pixman_transform_t transform;
	int32_t sx, sy;

	if (!output->current_mode)
		return;

	width = output->current_mode->width;
	height = output->current_mode->height;

	pixman_region32_init_rect(&damage, 0, 0, width, height);

	wlb_output_get_matrix(output, &transform);
	pixman_image_set_transform(image, &transform);

	if (output->surface.surface && output->surface.surface->buffer) {
		pixman_region32_init_rect(&surface_damage,
					  output->surface.position.x,
					  output->surface.position.y,
					  output->surface.position.width,
					  output->surface.position.height);

		buffer = wl_shm_buffer_get(output->surface.surface->buffer);
		assert(buffer);

		paint_shm_buffer(image, &surface_damage, buffer,
				 &output->surface.position);

		pixman_region32_subtract(&damage, &damage, &surface_damage);
		pixman_region32_fini(&surface_damage);
	}

	fill_with_black(pr, image, &damage);

	pixman_region32_fini(&damage);
}

