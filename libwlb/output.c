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
}

static void
output_bind(struct wl_client *client,
	    void *data, uint32_t version, uint32_t id)
{
	struct wlb_output *output = data;
	struct wlb_output_mode *mode;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_output_interface, 1, id);
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

	wl_list_init(&output->resource_list);
	output->global = wl_global_create(compositor->display,
					  &wl_output_interface, 1,
					  output, output_bind);
	if (!output->global)
		goto err_output;
	
	output->compositor = compositor;
	wl_list_insert(&compositor->output_list, &output->compositor_link);

	wl_list_init(&output->mode_list);

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

	wl_list_remove(&output->compositor_link);

	free(output->physical.make);
	free(output->physical.model);

	wl_list_for_each_safe(mode, next_mode, &output->mode_list, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}

	if (output->surface.surface)
		wl_list_remove(&output->surface.link);

	wl_global_destroy(output->global);
	wl_resource_for_each_safe(resource, next_res, &output->resource_list)
		wl_resource_destroy(resource);

	free(output);
}

WL_EXPORT void
wlb_output_set_transform(struct wlb_output *output,
			 enum wl_output_transform transform)
{
	struct wl_resource *resource;

	output->physical.transform = transform;

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
	return 1;
}

WL_EXPORT void
wlb_output_repaint_complete(struct wlb_output *output, uint32_t time)
{
	if (!output->surface.surface)
		return;

	if (output->surface.surface->primary_output != output)
		return;

	wlb_surface_post_frame_callbacks(output->surface.surface, time);
}

WL_EXPORT struct wlb_surface *
wlb_output_surface(struct wlb_output *output)
{
	return output->surface.surface;
}

WL_EXPORT uint32_t
wlb_output_present_method(struct wlb_output *output)
{
	return output->surface.present_method;
}

void
wlb_output_present_surface(struct wlb_output *output,
			   struct wlb_surface *surface,
			   enum wl_fullscreen_shell_present_method method,
			   int32_t framerate)
{
	if (output->surface.surface) {
		wl_list_remove(&output->surface.link);
		wlb_surface_compute_primary_output(output->surface.surface);
	}

	output->surface.surface = surface;
	output->surface.present_method = method;

	if (surface) {
		wlb_output_recompute_surface_position(output);
		wl_list_insert(&surface->output_list, &output->surface.link);
		wlb_surface_compute_primary_output(output->surface.surface);
	}
}

void
wlb_output_recompute_surface_position(struct wlb_output *output)
{
	int32_t ow, oh, sw, sh;

	assert(output->current_mode);
	assert(output->surface.surface);
	assert(output->surface.surface->width >= 0);
	assert(output->surface.surface->height >= 0);

	sw = output->surface.surface->width;
	sh = output->surface.surface->height;
	ow = output->current_mode->width;
	oh = output->current_mode->height;

	switch(output->surface.present_method) {
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_SCALE:
		if (ow / sw <= oh / sh) {
			output->surface.position.width = ow;
			output->surface.position.height =
				(sh * (int64_t)ow) / sw;
			output->surface.position.x = 0;
			output->surface.position.y =
				(oh - output->surface.position.height) / 2;
		} else {
			output->surface.position.width =
				(sw * (int64_t)oh) / sh;
			output->surface.position.height = oh;
			output->surface.position.x =
				(ow - output->surface.position.width) / 2;
			output->surface.position.y = 0;
		}

		break;
	default:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_FILL:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_DRIVER:
		output->surface.position.x = (ow - sw) / 2;
		output->surface.position.y = (oh - sh) / 2;
		output->surface.position.width = sw;
		output->surface.position.height = sh;
		break;
	}

	wlb_surface_compute_primary_output(output->surface.surface);
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
