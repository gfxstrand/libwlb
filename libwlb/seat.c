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
#include <stdio.h>

static void
seat_get_pointer(struct wl_client *client, struct wl_resource *resource,
		 uint32_t id)
{
	struct wlb_seat *seat = wl_resource_get_user_data(resource);

	if (!seat->pointer)
		return;
	
	wlb_pointer_create_resource(seat->pointer, client, id);
}

static void
seat_get_keyboard(struct wl_client *client, struct wl_resource *resource,
		  uint32_t id)
{
	struct wlb_seat *seat = wl_resource_get_user_data(resource);

	if (!seat->keyboard)
		return;
	
	wlb_keyboard_create_resource(seat->keyboard, client, id);
}

static void
seat_get_touch(struct wl_client *client, struct wl_resource *resource,
	       uint32_t id)
{
	struct wlb_seat *seat = wl_resource_get_user_data(resource);

	if (!seat->touch)
		return;

	wlb_touch_create_resource(seat->touch, client, id);
}

struct wl_seat_interface seat_interface = {
	seat_get_pointer,
	seat_get_keyboard,
	seat_get_touch
};

static void
seat_bind(struct wl_client *client,
	  void *data, uint32_t version, uint32_t id)
{
	struct wlb_seat *seat = data;
	struct wl_resource *resource;
	uint32_t capabilities = 0;

	resource = wl_resource_create(client, &wl_seat_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &seat_interface, seat, NULL);

	wl_list_insert(&seat->resource_list, wl_resource_get_link(resource));

	if (seat->touch)
		capabilities |= WL_SEAT_CAPABILITY_TOUCH;
	if (seat->keyboard)
		capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
	if (seat->pointer)
		capabilities |= WL_SEAT_CAPABILITY_POINTER;

	wl_seat_send_capabilities(resource, capabilities);
}

WL_EXPORT struct wlb_seat *
wlb_seat_create(struct wlb_compositor *compositor)
{
	struct wlb_seat *seat;

	seat = zalloc(sizeof *seat);
	if (!seat)
		return NULL;
	
	seat->compositor = compositor;
	
	seat->global = wl_global_create(compositor->display, &wl_seat_interface,
					1, seat, seat_bind);
	if (!seat->global)
		goto err_alloc;
	
	wl_list_init(&seat->resource_list);

	wl_list_insert(&compositor->seat_list, &seat->compositor_link);

	return seat;

err_alloc:
	free(seat);

	return NULL;
}

WL_EXPORT void
wlb_seat_destroy(struct wlb_seat *seat)
{
	wl_list_remove(&seat->compositor_link);

	if (seat->keyboard)
		wlb_keyboard_destroy(seat->keyboard);
	if (seat->pointer)
		wlb_pointer_destroy(seat->pointer);
	if (seat->touch)
		wlb_touch_destroy(seat->touch);

	free(seat);
}

void
wlb_seat_send_capabilities(struct wlb_seat *seat)
{
	struct wl_resource *resource;
	uint32_t capabilities = 0;

	if (seat->touch)
		capabilities |= WL_SEAT_CAPABILITY_TOUCH;
	if (seat->keyboard)
		capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
	if (seat->pointer)
		capabilities |= WL_SEAT_CAPABILITY_POINTER;

	wl_resource_for_each(resource, &seat->resource_list)
		wl_seat_send_capabilities(resource, capabilities);
}
