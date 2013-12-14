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

static void
compositor_create_surface(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id)
{
	struct wlb_compositor *compositor = wl_resource_get_user_data(resource);
	struct wlb_surface *surface;

	surface = wlb_surface_create(compositor, client, id);
	if (!surface)
		wl_client_post_no_memory(client);
}

static void
region_add(struct wl_client *client, struct wl_resource *resource,
	   int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);

	pixman_region32_union_rect(&region->region, &region->region,
				   x, y, width, height);
}

static void
region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);
	pixman_region32_t rect;

	pixman_region32_init_rect(&rect, x, y, width, height);
	pixman_region32_subtract(&region->region, &region->region, &rect);
	pixman_region32_fini(&rect);
}

static void
region_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_region_interface region_interface = {
	region_destroy,
	region_add,
	region_subtract
};

static void
destroy_region(struct wl_resource *resource)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);

	pixman_region32_fini(&region->region);
	free(region);
}

static void
compositor_create_region(struct wl_client *client,
			 struct wl_resource *resource, uint32_t id)
{
	struct wlb_region *region;

	region = zalloc(sizeof *region);
	if (!region) {
		wl_client_post_no_memory(client);
		return;
	}

	region->resource =
		wl_resource_create(client, &wl_region_interface, 1, id);
	if (!region->resource) {
		wl_client_post_no_memory(client);
		free(region);
		return;
	}
	wl_resource_set_implementation(region->resource, &region_interface,
				       region, destroy_region);

	pixman_region32_init(&region->region);
}

static const struct wl_compositor_interface compositor_interface = {
	compositor_create_surface,
	compositor_create_region
};

static void
compositor_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct wlb_compositor *comp = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_compositor_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &compositor_interface,
				       comp, NULL);
}

static void
shell_present_surface(struct wl_client *client, struct wl_resource *resource,
		      struct wl_resource *surface_res, uint32_t method,
		      uint32_t framerate, struct wl_resource *output_res)
{
	struct wlb_compositor *comp = wl_resource_get_user_data(resource);
	struct wlb_surface *surface = NULL;
	struct wlb_output *output = NULL;

	if (surface_res)
		surface = wl_resource_get_user_data(surface_res);
	if (output_res)
		output = wl_resource_get_user_data(output_res);
	
	if (output) {
		wlb_output_present_surface(output, surface, method, framerate);
	} else {
		wl_list_for_each(output, &comp->output_list, compositor_link)
			wlb_output_present_surface(output, surface,
						   method, framerate);
	}
}

static const struct wl_fullscreen_shell_interface fullscreen_shell_interface = {
	shell_present_surface
};

static void
fullscreen_shell_bind(struct wl_client *client,
		      void *data, uint32_t version, uint32_t id)
{
	struct wlb_compositor *comp = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_fullscreen_shell_interface,
				      1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &fullscreen_shell_interface,
				       comp, NULL);
}

static int
shm_buffer_size_func(void *data, struct wl_resource *buffer,
		     int32_t *width, int32_t *height)
{
	struct wl_shm_buffer *shm_buffer;

	shm_buffer = wl_shm_buffer_get(buffer);
	if (!shm_buffer)
		return 0;

	*width = wl_shm_buffer_get_width(shm_buffer);
	*height = wl_shm_buffer_get_height(shm_buffer);

	return 1;
}

WL_EXPORT struct wlb_compositor *
wlb_compositor_create(struct wl_display *display)
{
	struct wlb_compositor *comp;

	comp = zalloc(sizeof *comp);
	if (!comp)
		return NULL;
	
	comp->display = display;

	wl_list_init(&comp->buffer_type_list);

	wl_list_init(&comp->output_list);
	wl_list_init(&comp->seat_list);
	
	if (!wl_global_create(display, &wl_compositor_interface, 1,
			      comp, compositor_bind))
		goto err_alloc;

	if (!wl_global_create(display, &wl_fullscreen_shell_interface, 1,
			      comp, fullscreen_shell_bind))
		goto err_alloc;
	
	wlb_compositor_add_buffer_type(comp, shm_buffer_size_func, NULL);

	return comp;

err_alloc:
	free(comp);
	return NULL;
}

WL_EXPORT void
wlb_compositor_destroy(struct wlb_compositor *comp)
{
	struct wlb_output *output, *onext;
	struct wlb_seat *seat, *snext;

	wl_list_for_each_safe(output, onext, &comp->output_list, compositor_link)
		wlb_output_destroy(output);

	wl_list_for_each_safe(seat, snext, &comp->seat_list, compositor_link)
		wlb_seat_destroy(seat);
	
	wl_display_destroy(comp->display);

	free(comp);
}

WL_EXPORT int
wlb_compositor_add_buffer_type(struct wlb_compositor *comp,
			       wlb_buffer_size_func_t func, void *data)
{
	struct wlb_buffer_type *type;

	type = zalloc(sizeof *type);
	if (!type)
		return -1;

	type->func = func;
	type->data = data;

	wl_list_insert(&comp->buffer_type_list, &type->link);

	return 0;
}

int
wlb_compositor_get_buffer_size(struct wlb_compositor *comp,
			       struct wl_resource *buffer,
			       int32_t *width, int32_t *height)
{
	struct wlb_buffer_type *type;
	int32_t tw, th;

	wl_list_for_each(type, &comp->buffer_type_list, link) {
		if (type->func(type->data, buffer, &tw, &th) > 0) {
			*width = tw;
			*height = th;
			return 1;
		}
	}

	return 0;
}
