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
	struct wlb_callback *callback;

	callback = wlb_callback_create(client, callback_id);
	if (!callback)
		wl_client_post_no_memory(client);
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

	if (surface->buffer) {
		if (surface->buffer != surface->pending.buffer)
			wl_buffer_send_release(surface->buffer);
		wl_list_remove(&surface->buffer_destroy_listener.link);
	}

	surface->buffer = surface->pending.buffer;

	if (surface->buffer)
		wl_resource_add_destroy_listener(surface->buffer,
						 &surface->buffer_destroy_listener);
	
	pixman_region32_union(&surface->damage, &surface->damage, 
			      &surface->pending.damage);
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
	struct wlb_callback *callback, *next;

	if (surface->pending.buffer)
		wl_list_remove(&surface->pending.buffer_destroy_listener.link);
	
	pixman_region32_fini(&surface->pending.damage);
	pixman_region32_fini(&surface->pending.input_region);

	wl_list_for_each_safe(callback, next,
			      &surface->pending.frame_callbacks, link)
		wlb_callback_destroy(callback);

	if (surface->buffer)
		wl_list_remove(&surface->buffer_destroy_listener.link);

	pixman_region32_fini(&surface->damage);
	pixman_region32_fini(&surface->input_region);

	wl_list_for_each_safe(callback, next, &surface->frame_callbacks, link)
		wlb_callback_destroy(callback);

	free(surface);
}

struct wlb_surface *
wlb_surface_create(struct wl_client *client, uint32_t id)
{
	struct wlb_surface *surface;

	surface = zalloc(sizeof *surface);
	if (!surface)
		return NULL;
	
	surface->resource =
		wl_resource_create(client, &wl_surface_interface, 1, id);
	if (!surface->resource) {
		wl_client_post_no_memory(client);
		return NULL;
	}

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
	wl_list_init(&surface->pending.frame_callbacks);

	wl_resource_set_implementation(surface->resource, &surface_interface,
				       surface, surface_resource_destroyed);

	return surface;
}
