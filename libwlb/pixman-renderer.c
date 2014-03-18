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
		 struct wl_shm_buffer *buffer,
		 enum wl_output_transform buffer_transform,
		 struct wlb_rectangle *pos)
{
	pixman_format_code_t format;
	pixman_image_t *buffer_image;
	pixman_transform_t transform;
	pixman_fixed_t fw, fh;
	uint32_t bw, bh; /* Buffer size before/after roatation */

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

	bw = wl_shm_buffer_get_width(buffer);
	bh = wl_shm_buffer_get_height(buffer);

	buffer_image =
		pixman_image_create_bits(format, bw, bh,
					 wl_shm_buffer_get_data(buffer),
					 wl_shm_buffer_get_stride(buffer));

	fw = pixman_int_to_fixed(bw);
	fh = pixman_int_to_fixed(bh);

	pixman_transform_init_identity(&transform);

	switch (buffer_transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		pixman_transform_scale(&transform, NULL,
				       fw / pos->width, fh / pos->height);

		if (bw != pos->width || bh != pos->height)
			pixman_image_set_filter(buffer_image,
						PIXMAN_FILTER_BILINEAR,
						NULL, 0);
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		pixman_transform_scale(&transform, NULL,
				       fh / pos->width, fw / pos->height);

		if (bh != pos->width || bw != pos->height)
			pixman_image_set_filter(buffer_image,
						PIXMAN_FILTER_BILINEAR,
						NULL, 0);
		break;
	}

	switch (buffer_transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		pixman_transform_rotate(&transform, NULL, 0, pixman_fixed_1);
		pixman_transform_translate(&transform, NULL, fw, 0);
		break;
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		pixman_transform_rotate(&transform, NULL, -pixman_fixed_1, 0);
		pixman_transform_translate(&transform, NULL, fw, fh);
		break;
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		pixman_transform_rotate(&transform, NULL, 0, -pixman_fixed_1);
		pixman_transform_translate(&transform, NULL, 0, fh);
		break;
	}

	switch (buffer_transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_270:
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		pixman_transform_scale(&transform, NULL,
				       -pixman_fixed_1, pixman_fixed_1);
		pixman_transform_translate(&transform, NULL, fw, 0);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		pixman_transform_scale(&transform, NULL,
				       pixman_fixed_1, -pixman_fixed_1);
		pixman_transform_translate(&transform, NULL, 0, fh);
		break;
	}

	pixman_image_set_transform(buffer_image, &transform);

	pixman_image_set_clip_region32(image, region);

	pixman_image_composite32(PIXMAN_OP_SRC,
				 buffer_image, /* src_img */
				 NULL, /* mask_img */
				 image, /* dest_img */
				 0, 0, /* src_x/y */
				 0, 0, /* mask_x/y */
				 pos->x, pos->y, /* dest_x/y */
				 pos->width, pos->height); /* dest_w/h */

	pixman_image_set_clip_region32(image, NULL);

	pixman_image_unref(buffer_image);
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
	struct wlb_rectangle pos;

	if (!output->current_mode)
		return;

	width = output->current_mode->width;
	height = output->current_mode->height;

	pixman_region32_init_rect(&damage, 0, 0, width, height);

	wlb_output_get_matrix(output, &transform);
	pixman_image_set_transform(image, &transform);

	if (output->surface.surface && output->surface.surface->buffer) {
		pos.x = output->surface.position.x * output->scale;
		pos.y = output->surface.position.y * output->scale;
		pos.width = output->surface.position.width * output->scale;
		pos.height = output->surface.position.height * output->scale;

		pixman_region32_init_rect(&surface_damage,
					  pos.x,
					  pos.y,
					  pos.width,
					  pos.height);

		buffer = wl_shm_buffer_get(output->surface.surface->buffer);
		assert(buffer);

		paint_shm_buffer(image, &surface_damage, buffer,
				 wlb_surface_buffer_transform(output->surface.surface),
				 &pos);

		pixman_region32_subtract(&damage, &damage, &surface_damage);
		pixman_region32_fini(&surface_damage);
	}

	fill_with_black(pr, image, &damage);

	pixman_region32_fini(&damage);
}

