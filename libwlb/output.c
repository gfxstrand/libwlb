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
#include "wlb-private.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void
output_resource_destroyed(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
output_send_mode(struct wlb_output *output, struct wl_resource *resource,
		 struct wlb_output_mode *mode)
{
	uint32_t mode_flags;

	mode_flags = 0;
	if (mode == output->current_mode)
		mode_flags |= WL_OUTPUT_MODE_CURRENT;
	if (mode == output->preferred_mode)
		mode_flags |= WL_OUTPUT_MODE_PREFERRED;

	wl_output_send_mode(resource, mode_flags,
			    mode->width, mode->height, mode->refresh);
}

static void
output_send_geometry(struct wlb_output *output, struct wl_resource *resource)
{
	wl_output_send_geometry(resource, output->x, output->y,
				output->physical.width, output->physical.height,
				output->physical.subpixel,
				output->physical.make, output->physical.model,
				output->physical.transform);

	if (wl_resource_get_version(resource) >= 2) {
		wl_output_send_scale(resource, output->scale);
		wl_output_send_done(resource);
	}
}

static void
output_update_geometry(struct wlb_output *output)
{
	int width, height;

	if (!output->current_mode)
		return;

	switch (output->physical.transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		width = output->current_mode->width;
		height = output->current_mode->height;
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		width = output->current_mode->height;
		height = output->current_mode->width;
		break;
	}

	width /= output->scale;
	height /= output->scale;

	if (output->width != width || output->height != height) {
		output->width = width;
		output->height = height;

		wl_signal_emit(&output->geometry_changed_signal, output);
	}
}

static void
output_bind(struct wl_client *client,
	    void *data, uint32_t version, uint32_t id)
{
	struct wlb_output *output = data;
	struct wlb_output_mode *mode;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_output_interface,
				      WLB_MIN(2, version), id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, NULL,
				       output, output_resource_destroyed);
	wl_list_insert(&output->resource_list, wl_resource_get_link(resource));

	output_send_geometry(output, resource);
	
	wl_list_for_each(mode, &output->mode_list, link)
		output_send_mode(output, resource, mode);
}

WL_EXPORT struct wlb_output *
wlb_output_create(struct wlb_compositor *compositor, int32_t width,
		  int32_t height, const char *make, const char *model)
{
	struct wlb_output *output;

	output = zalloc(sizeof *output);
	if (!output)
		return NULL;
	
	output->physical.width = width;
	output->physical.height = height;
	if (make) {
		output->physical.make = strdup(make);
		if (!output->physical.make)
			goto err_output;
	}
	if (model) {
		output->physical.model = strdup(model);
		if (!output->physical.model)
			goto err_output;
	}
	output->physical.transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->physical.subpixel = WL_OUTPUT_SUBPIXEL_UNKNOWN;

	output->scale = 1;

	wl_signal_init(&output->destroy_signal);

	wl_list_init(&output->resource_list);
	output->global = wl_global_create(compositor->display,
					  &wl_output_interface, 2,
					  output, output_bind);
	if (!output->global)
		goto err_output;
	
	output->compositor = compositor;
	wl_list_insert(&compositor->output_list, &output->compositor_link);

	wl_list_init(&output->mode_list);
	wl_signal_init(&output->geometry_changed_signal);

	pixman_region32_init(&output->damage);
	wl_list_init(&output->pending_frame_callbacks);

	return output;

err_output:
	free(output->physical.make);
	free(output->physical.model);
	free(output);

	return NULL;
}

WL_EXPORT void
wlb_output_destroy(struct wlb_output *output)
{
	struct wlb_output_mode *mode, *next_mode;
	struct wl_resource *resource, *next_res;

	wl_signal_emit(&output->destroy_signal, output);

	wl_list_remove(&output->compositor_link);

	free(output->physical.make);
	free(output->physical.model);

	wl_list_for_each_safe(mode, next_mode, &output->mode_list, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}

	wlb_output_set_surface(output, NULL, NULL);

	wl_global_destroy(output->global);
	wl_resource_for_each_safe(resource, next_res, &output->resource_list)
		wl_resource_destroy(resource);

	free(output);
}

WL_EXPORT void
wlb_output_set_funcs_with_size(struct wlb_output *output,
			       struct wlb_output_funcs *funcs, void *data,
			       size_t size)
{
	output->funcs = funcs;
	output->funcs_data = data;
	output->funcs_size = size;
}

WL_EXPORT void
wlb_output_set_transform(struct wlb_output *output,
			 enum wl_output_transform transform)
{
	struct wl_resource *resource;

	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		break;
	default:
		wlb_error("wlb_outpu_set_transform: Invalid transform: %d\n",
			  transform);
		return;
	}

	output->physical.transform = transform;

	output_update_geometry(output);

	wl_resource_for_each(resource, &output->resource_list)
		output_send_geometry(output, resource);
}

WL_EXPORT void
wlb_output_set_scale(struct wlb_output *output, int32_t scale)
{
	struct wl_resource *resource;

	if (scale <= 0) {
		wlb_error("wlb_outpu_set_scale: Invalid scale: %d\n", scale);
		return;
	}

	output->scale = scale;

	output_update_geometry(output);

	wl_resource_for_each(resource, &output->resource_list)
		output_send_geometry(output, resource);
}

WL_EXPORT void
wlb_output_set_subpixel(struct wlb_output *output,
			 enum wl_output_subpixel subpixel)
{
	struct wl_resource *resource;

	output->physical.subpixel = subpixel;

	wl_resource_for_each(resource, &output->resource_list)
		output_send_geometry(output, resource);
}

static struct wlb_output_mode *
output_get_mode(struct wlb_output *output,
		int32_t width, int32_t height, int32_t refresh)
{
	struct wlb_output_mode *mode;

	wl_list_for_each(mode, &output->mode_list, link) {
		if (mode->width == width && mode->height == height &&
		    mode->refresh == refresh)
			return mode;
	}

	mode = zalloc(sizeof *mode);
	if (!mode)
		return NULL;
	
	mode->width = width;
	mode->height = height;
	mode->refresh = refresh;

	wl_list_insert(output->mode_list.prev, &mode->link);

	return mode;
}

WL_EXPORT void
wlb_output_add_mode(struct wlb_output *output,
		    int32_t width, int32_t height, int32_t refresh)
{
	output_get_mode(output, width, height, refresh);
}

WL_EXPORT void
wlb_output_set_mode(struct wlb_output *output,
		    int32_t width, int32_t height, int32_t refresh)
{
	struct wlb_output_mode *mode;
	struct wl_resource *resource;

	mode = output_get_mode(output, width, height, refresh);
	if (!mode)
		return;

	output->current_mode = mode;

	output_update_geometry(output);

	pixman_region32_fini(&output->damage);
	pixman_region32_init_rect(&output->damage, 0, 0, width, height);

	wl_resource_for_each(resource, &output->resource_list)
		output_send_mode(output, resource, mode);
}

WL_EXPORT void
wlb_output_set_preferred_mode(struct wlb_output *output,
			      int32_t width, int32_t height, int32_t refresh)
{
	struct wlb_output_mode *mode;
	struct wl_resource *resource;

	mode = output_get_mode(output, width, height, refresh);
	if (!mode)
		return;

	output->preferred_mode = mode;

	wl_resource_for_each(resource, &output->resource_list)
		output_send_mode(output, resource, mode);
}

WL_EXPORT int
wlb_output_needs_repaint(struct wlb_output *output)
{
	return pixman_region32_not_empty(&output->damage);
}

WL_EXPORT void
wlb_output_prepare_frame(struct wlb_output *output)
{
	if (!output->surface.surface)
		return;

	if (output->surface.surface->primary_output != output)
		return;

	wl_list_insert_list(&output->pending_frame_callbacks,
			    &output->surface.surface->frame_callbacks);
	wl_list_init(&output->surface.surface->frame_callbacks);
}

WL_EXPORT void
wlb_output_frame_complete(struct wlb_output *output, uint32_t time)
{
	struct wlb_callback *callback, *next;

	/* Clear damage */
	pixman_region32_fini(&output->damage);
	pixman_region32_init(&output->damage);

	wl_list_for_each_safe(callback, next,
			      &output->pending_frame_callbacks, link)
		wlb_callback_notify(callback, time);
	wl_display_flush_clients(output->compositor->display);
}

WL_EXPORT struct wlb_surface *
wlb_output_surface(struct wlb_output *output)
{
	return output->surface.surface;
}

WL_EXPORT void
wlb_output_surface_position(struct wlb_output *output, int32_t *x, int32_t *y,
			    uint32_t *width, uint32_t *height)
{
	if (x)
		*x = output->surface.position.x;
	if (y)
		*y = output->surface.position.y;
	if (width)
		*width = output->surface.position.width;
	if (height)
		*height = output->surface.position.height;
}

static void
output_surface_committed(struct wl_listener *listener, void *data)
{
	struct wlb_output *output;
	pixman_box32_t *srects, *orects;
	pixman_region32_t odamage;
	int i, nrects;
	int32_t x, y, ow, oh, sw, sh;

	output = wl_container_of(listener, output, surface.committed);

	srects = pixman_region32_rectangles(&output->surface.surface->damage,
					    &nrects);
	orects = malloc(nrects * sizeof(*orects));
	if (!orects)
		return;

	x = output->surface.position.x;
	y = output->surface.position.y;
	ow = output->surface.position.width;
	oh = output->surface.position.height;
	sw = output->surface.surface->width;
	sh = output->surface.surface->height;

	for (i = 0; i < nrects; ++i) {
		orects[i].x1 = x + (srects[i].x1 * ow) / sw;
		orects[i].y1 = y + (srects[i].y1 * oh) / sh;
		orects[i].x2 = x + (srects[i].x2 * ow + sw - 1) / sw;
		orects[i].y2 = y + (srects[i].y2 * oh + sh - 1) / sh;
	}

	pixman_region32_init_rects(&odamage, orects, nrects);
	free(orects);
	pixman_region32_union(&output->damage, &output->damage, &odamage);
	pixman_region32_fini(&odamage);
}

void
wlb_output_set_surface(struct wlb_output *output, struct wlb_surface *surface,
		       const struct wlb_rectangle *pos)
{
	int pos_changed;

	if (output->surface.surface && output->surface.surface != surface) {
		wl_list_remove(&output->surface.link);
		wl_list_remove(&output->surface.committed.link);
		wlb_surface_compute_primary_output(output->surface.surface);
	}

	if (pos) {
		pos_changed = output->surface.position.x != pos->x ||
			      output->surface.position.y != pos->y ||
			      output->surface.position.width != pos->width ||
			      output->surface.position.height != pos->height;
	} else {
		pos_changed = 0;
	}

	if (output->surface.surface &&
	    (output->surface.surface != surface || pos_changed))
		/* Damage where the surface was */
		pixman_region32_union_rect(&output->damage, &output->damage,
					   output->surface.position.x,
					   output->surface.position.y,
					   output->surface.position.width,
					   output->surface.position.height);

	if (pos_changed)
		output->surface.position = *pos;

	if (surface && (output->surface.surface != surface || pos_changed))
		/* Damage where the surface is now */
		pixman_region32_union_rect(&output->damage, &output->damage,
					   output->surface.position.x,
					   output->surface.position.y,
					   output->surface.position.width,
					   output->surface.position.height);

	if (surface && output->surface.surface != surface) {
		wl_list_insert(&surface->output_list, &output->surface.link);
		output->surface.committed.notify = output_surface_committed;
		wl_signal_add(&surface->commit_signal,
			      &output->surface.committed);
		wlb_surface_compute_primary_output(surface);
	}

	output->surface.surface = surface;
}

void
wlb_output_get_matrix(struct wlb_output *output,
		      pixman_transform_t *transform)
{
	pixman_fixed_t fw, fh;

	pixman_transform_init_identity(transform);

	assert(output->current_mode);

	fw = pixman_int_to_fixed(output->current_mode->width);
	fh = pixman_int_to_fixed(output->current_mode->height);

	switch(output->physical.transform) {
	default:
	case WL_OUTPUT_TRANSFORM_NORMAL:
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		break;
	case WL_OUTPUT_TRANSFORM_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		pixman_transform_rotate(transform, NULL, 0, -pixman_fixed_1);
		pixman_transform_translate(transform, NULL, 0, fh);
		break;
	case WL_OUTPUT_TRANSFORM_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		pixman_transform_rotate(transform, NULL, -pixman_fixed_1, 0);
		pixman_transform_translate(transform, NULL, fw, fh);
		break;
	case WL_OUTPUT_TRANSFORM_270:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		pixman_transform_rotate(transform, NULL, 0, pixman_fixed_1);
		pixman_transform_translate(transform, NULL, fw, 0);
		break;
	}

	switch (output->physical.transform) {
	case WL_OUTPUT_TRANSFORM_FLIPPED:
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		pixman_transform_scale(transform, NULL,
				       pixman_int_to_fixed (-1),
				       pixman_int_to_fixed (1));
		pixman_transform_translate(transform, NULL, fw, 0);
		break;
	default:
		break;
	}
}

void
wlb_output_to_surface_coords(struct wlb_output *output,
			     wl_fixed_t ox, wl_fixed_t oy,
			     wl_fixed_t *sx, wl_fixed_t *sy)
{
	if (!output->current_mode)
		return;

	ox -= wl_fixed_from_int(output->surface.position.x);
	oy -= wl_fixed_from_int(output->surface.position.y);

	if (sx)
		*sx = (ox * (int64_t)output->surface.surface->width) /
		      output->surface.position.width;
	if (sy)
		*sy = (oy * (int64_t)output->surface.surface->height) /
		      output->surface.position.height;
}

void
wlb_output_from_device_coords(struct wlb_output *output,
			      wl_fixed_t dx, wl_fixed_t dy,
			      wl_fixed_t *ox_dest, wl_fixed_t *oy_dest)
{
	wl_fixed_t ox, oy;

	dx /= output->scale;
	dy /= output->scale;

	switch(output->physical.transform) {
	default:
	case WL_OUTPUT_TRANSFORM_NORMAL:
		ox = dx;
		oy = dy;
		break;
	case WL_OUTPUT_TRANSFORM_90:
		ox = dy;
		oy = wl_fixed_from_int(output->height) - dx;
		break;
	case WL_OUTPUT_TRANSFORM_180:
		ox = wl_fixed_from_int(output->width) - dx;
		oy = wl_fixed_from_int(output->height) - dy;
		break;
	case WL_OUTPUT_TRANSFORM_270:
		ox = wl_fixed_from_int(output->width) - dy;
		oy = dx;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		ox = wl_fixed_from_int(output->width) - dx;
		oy = dy;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		ox = wl_fixed_from_int(output->width) - dy;
		oy = wl_fixed_from_int(output->height) - dx;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		ox = dx;
		oy = wl_fixed_from_int(output->height) - dy;
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		ox = dy;
		oy = dx;
		break;
	}

	if (ox_dest)
		*ox_dest = ox / output->scale;
	if (oy_dest)
		*oy_dest = oy / output->scale;
}

struct wlb_output *
wlb_output_find(struct wlb_compositor *c, wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_output *output;
	int32_t ix, iy;

	ix = wl_fixed_to_int(x);
	iy = wl_fixed_to_int(y);

	wl_list_for_each(output, &c->output_list, compositor_link) {
		if (ix >= output->x && iy >= output->y &&
		    ix < output->x + output->width &&
		    iy < output->y + output->height)
			continue;
	}

	return NULL;
}

struct wlb_output *
wlb_output_find_with_surface(struct wlb_compositor *c,
			     wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_output *output;
	int32_t ix, iy;
	wl_fixed_t sx, sy;

	ix = wl_fixed_to_int(x);
	iy = wl_fixed_to_int(y);

	wl_list_for_each(output, &c->output_list, compositor_link) {
		if (!output->surface.surface)
			continue;

		if (ix < output->x || iy < output->y ||
		    ix >= output->x + output->width ||
		    iy >= output->y + output->height)
			continue;

		if (ix < output->surface.position.x ||
		    iy < output->surface.position.y ||
		    ix >= output->surface.position.x + (int32_t)output->surface.position.width ||
		    iy >= output->surface.position.y + (int32_t)output->surface.position.height)
			continue;

		wlb_output_to_surface_coords(output, x, y, &sx, &sy);

		if (pixman_region32_contains_point(&output->surface.surface->input_region,
						   wl_fixed_from_int(sx),
						   wl_fixed_from_int(sy),
						   NULL))
			return output;
	}

	return NULL;
}
