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
callback_resource_destroyed(struct wl_resource *resource)
{
	struct wlb_callback *callback = wl_resource_get_user_data(resource);

	wl_list_remove(&callback->link);
	free(callback);
}

static void
wlb_callback_destroy(struct wlb_callback *callback)
{
	wl_resource_destroy(callback->resource);
}

static void
wlb_callback_notify(struct wlb_callback *callback, uint32_t serial)
{
	wl_callback_send_done(callback->resource, serial);
	wlb_callback_destroy(callback);
}

static struct wlb_callback *
wlb_callback_create(struct wl_client *client, uint32_t id)
{
	struct wlb_callback *callback;

	callback = zalloc(sizeof *callback);
	if (!callback)
		return NULL;

	callback->resource =
		wl_resource_create(client, &wl_callback_interface, 1, id);
	if (!callback->resource) {
		free(callback);
		return NULL;
	}

	wl_resource_set_implementation(callback->resource, NULL,
				       callback, callback_resource_destroyed);

	return callback;
}

static void
surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
surface_pending_buffer_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_surface *surface;

	surface = wl_container_of(listener, surface,
				  pending.buffer_destroy_listener);
	surface->pending.buffer = NULL;
}

static void
surface_attach(struct wl_client *client, struct wl_resource *resource,
	       struct wl_resource *buffer, int32_t x, int32_t y)
{
	struct wlb_surface *surface = wl_resource_get_user_data(resource);
	
	if (surface->pending.buffer)
		wl_list_remove(&surface->pending.buffer_destroy_listener.link);

	surface->pending.buffer = buffer;

	if (surface->pending.buffer)
		wl_resource_add_destroy_listener(buffer, &surface->pending.buffer_destroy_listener);
}

static void
surface_damage(struct wl_client *client, struct wl_resource *resource,
	       int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wlb_surface *surface = wl_resource_get_user_data(resource);

	pixman_region32_union_rect(&surface->pending.damage,
				   &surface->pending.damage,
				   x, y, width, height);
}

static void
surface_frame(struct wl_client *client, struct wl_resource *resource,
	      uint32_t callback_id)
{
	struct wlb_surface *surface = wl_resource_get_user_data(resource);
	struct wlb_callback *callback;

	callback = wlb_callback_create(client, callback_id);
	if (!callback)
		wl_client_post_no_memory(client);
	wl_list_insert(&surface->pending.frame_callbacks, &callback->link);
}

static void
surface_set_opaque_region(struct wl_client *client,
			  struct wl_resource *resource,
			  struct wl_resource *region_res)
{
	/* Unused since everything is fullscreen */
}

static void
surface_set_input_region(struct wl_client *client,
			 struct wl_resource *resource,
			 struct wl_resource *region_res)
{
	struct wlb_surface *surface = wl_resource_get_user_data(resource);
	struct wlb_region *region = wl_resource_get_user_data(region_res);

	pixman_region32_copy(&surface->pending.input_region, &region->region);
}

static void
surface_buffer_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_surface *surface;

	surface = wl_container_of(listener, surface, buffer_destroy_listener);
	surface->buffer = NULL;
}

static void
surface_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct wlb_surface *surface = wl_resource_get_user_data(resource);
	struct wl_shm_buffer *shm_buffer;
	int32_t old_width, old_height;
	struct wlb_output *output;

	if (surface->buffer) {
		if (surface->buffer != surface->pending.buffer)
			wl_buffer_send_release(surface->buffer);
		wl_list_remove(&surface->buffer_destroy_listener.link);
	}

	surface->buffer = surface->pending.buffer;

	if (!surface->buffer) {
		surface->width = 0;
		surface->height = 0;
	} else if (shm_buffer = wl_shm_buffer_get(surface->buffer)) {
		old_width = surface->width;
		old_height = surface->height;
		surface->width = wl_shm_buffer_get_width(shm_buffer);
		surface->height = wl_shm_buffer_get_height(shm_buffer);
		if (old_width != surface->width || old_height != surface->height)
			wl_list_for_each(output, &surface->output_list, surface.link)
				wlb_output_recompute_surface_position(output);
	} else {
		surface->width = -1;
		surface->height = -1;
	}

	if (surface->buffer)
		wl_resource_add_destroy_listener(surface->buffer,
						 &surface->buffer_destroy_listener);
	
	pixman_region32_union(&surface->damage, &surface->damage, 
			      &surface->pending.damage);
	pixman_region32_fini(&surface->pending.damage);
	pixman_region32_init(&surface->pending.damage);
	pixman_region32_copy(&surface->input_region,
			     &surface->pending.input_region);
	wl_list_insert_list(&surface->frame_callbacks,
			    &surface->pending.frame_callbacks);
	wl_list_init(&surface->pending.frame_callbacks);
}

static const struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
};

static void
surface_resource_destroyed(struct wl_resource *resource)
{
	wlb_surface_destroy(wl_resource_get_user_data(resource));
}

void
wlb_surface_destroy(struct wlb_surface *surface)
{
	struct wlb_callback *callback, *cnext;
	struct wlb_output *output, *onext;

	wl_list_for_each_safe(output, onext, &surface->output_list, surface.link)
		wlb_output_present_surface(output, NULL, 0, 0);

	if (surface->pending.buffer)
		wl_list_remove(&surface->pending.buffer_destroy_listener.link);
	
	pixman_region32_fini(&surface->pending.damage);
	pixman_region32_fini(&surface->pending.input_region);

	wl_list_for_each_safe(callback, cnext,
			      &surface->pending.frame_callbacks, link)
		wlb_callback_destroy(callback);

	if (surface->buffer)
		wl_list_remove(&surface->buffer_destroy_listener.link);

	pixman_region32_fini(&surface->damage);
	pixman_region32_fini(&surface->input_region);

	wl_list_for_each_safe(callback, cnext, &surface->frame_callbacks, link)
		wlb_callback_destroy(callback);

	free(surface);
}

struct wlb_surface *
wlb_surface_create(struct wlb_compositor *compositor,
		   struct wl_client *client, uint32_t id)
{
	struct wlb_surface *surface;

	surface = zalloc(sizeof *surface);
	if (!surface)
		return NULL;

	surface->compositor = compositor;

	surface->resource =
		wl_resource_create(client, &wl_surface_interface, 1, id);
	if (!surface->resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

	wl_list_init(&surface->output_list);

	surface->pending.buffer_destroy_listener.notify =
		surface_pending_buffer_destroyed;
	pixman_region32_init(&surface->pending.damage);
	pixman_region32_init_rect(&surface->pending.input_region,
				  INT32_MIN, INT32_MIN,
				  UINT32_MAX, UINT32_MAX);
	wl_list_init(&surface->pending.frame_callbacks);

	surface->buffer_destroy_listener.notify = surface_buffer_destroyed;
	pixman_region32_init(&surface->damage);
	pixman_region32_init_rect(&surface->input_region,
				  INT32_MIN, INT32_MIN,
				  UINT32_MAX, UINT32_MAX);
	wl_list_init(&surface->frame_callbacks);

	wl_resource_set_implementation(surface->resource, &surface_interface,
				       surface, surface_resource_destroyed);

	return surface;
}

void
wlb_surface_compute_primary_output(struct wlb_surface *surface)
{
	struct wlb_output *output;
	uint32_t area, max;

	surface->primary_output = NULL;
	max = 0;
	wl_list_for_each(output, &surface->output_list, surface.link) {
		area = output->surface.position.width * output->surface.position.height;
		if (area > max) {
			area = max;
			surface->primary_output = output;
		}
	}
}

void
wlb_surface_post_frame_callbacks(struct wlb_surface *surface, uint32_t time)
{
	struct wlb_callback *callback, *next;

	wl_list_for_each_safe(callback, next, &surface->frame_callbacks, link)
		wlb_callback_notify(callback, time);
}

WL_EXPORT void
wlb_surface_add_destroy_listener(struct wlb_surface *surface,
				 struct wl_listener *listener)
{
	wl_resource_add_destroy_listener(surface->resource, listener);
}

WL_EXPORT struct wl_listener *
wlb_surface_get_destroy_listener(struct wlb_surface *surface,
				 wl_notify_func_t notify)
{
	return wl_resource_get_destroy_listener(surface->resource, notify);
}

WL_EXPORT struct wlb_rectangle *
wlb_surface_get_buffer_damage(struct wlb_surface *surface, int *nrects)
{
	struct wlb_rectangle *rects;
	pixman_box32_t *drects;
	int dnrects, i;

	if (!pixman_region32_not_empty(&surface->damage)) {
		if (nrects)
			*nrects = 0;
		return NULL;
	}

	drects = pixman_region32_rectangles(&surface->damage, &dnrects);

	if (nrects)
		*nrects = dnrects;

	rects = malloc(dnrects * sizeof(*rects));
	if (!rects)
		return NULL;

	memcpy(rects, drects, dnrects * sizeof(*rects));

	for (i = 0; i < dnrects; ++i) {
		rects[i].width -= rects[i].x;
		rects[i].height -= rects[i].y;
	}

	return rects;
}

WL_EXPORT void
wlb_surface_reset_damage(struct wlb_surface *surface)
{
	pixman_region32_fini(&surface->damage);
	pixman_region32_init(&surface->damage);
}

WL_EXPORT struct wl_resource *
wlb_surface_buffer(struct wlb_surface *surface)
{
	return surface->buffer;
}

WL_EXPORT enum wl_output_transform
wlb_surface_buffer_transform(struct wlb_surface *surface)
{
	return WL_OUTPUT_TRANSFORM_NORMAL;
}

WL_EXPORT int32_t
wlb_surface_buffer_scale(struct wlb_surface *surface)
{
	return 1;
}

