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

/* Common destructor used by all seat resources */
static void
unlink_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

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
}

static void
seat_get_touch(struct wl_client *client, struct wl_resource *resource,
	       uint32_t id)
{
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

	resource = wl_resource_create(client, &wl_seat_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &seat_interface, seat, NULL);

	wl_seat_send_capabilities(resource, seat->capabilities);
}

WL_EXPORT struct wlb_seat *
wlb_seat_create(struct wlb_compositor *compositor, uint32_t capabilities)
{
	struct wlb_seat *seat;

	seat = zalloc(sizeof *seat);
	if (!seat)
		return NULL;
	
	seat->compositor = compositor;
	seat->capabilities = capabilities;
	
	seat->global = wl_global_create(compositor->display, &wl_seat_interface,
					1, seat, seat_bind);
	if (!seat->global)
		goto err_alloc;

	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		seat->pointer = wlb_pointer_create(seat);
		if (!seat->pointer)
			goto err_global;
	}

	wl_list_insert(&compositor->seat_list, &seat->compositor_link);

	return seat;

err_global:
	wl_global_destroy(seat->global);
err_alloc:
	free(seat);

	return NULL;
}

WL_EXPORT void
wlb_seat_destroy(struct wlb_seat *seat)
{
	wl_list_remove(&seat->compositor_link);

	if (seat->pointer)
		wlb_pointer_destroy(seat->pointer);

	free(seat);
}

WL_EXPORT void
wlb_seat_pointer_motion_relative(struct wlb_seat *seat, uint32_t time,
				 wl_fixed_t dx, wl_fixed_t dy)
{
	if (!seat->pointer)
		return;

	wlb_seat_pointer_motion_absolute(seat, time, seat->pointer->x + dx,
					 seat->pointer->y + dy);
}

WL_EXPORT void
wlb_seat_pointer_motion_absolute(struct wlb_seat *seat, uint32_t time,
				 wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_output *output;

	if (!seat->pointer)
		return;
	
	seat->pointer->x = x;
	seat->pointer->y = y;
	
	if (seat->pointer->button_count > 0) {
		output = wlb_output_find_with_surface(seat->compositor, x, y);
		if (seat->pointer->focus != output)
			wlb_pointer_set_focus(seat->pointer, output);
	}

	wlb_pointer_send_motion(seat->pointer, time, x, y);
}

WL_EXPORT void
wlb_seat_pointer_button(struct wlb_seat *seat, uint32_t time, uint32_t button,
			enum wl_pointer_button_state state)
{
	if (!seat->pointer)
		return;

	wlb_pointer_send_button(seat->pointer, time, button, state);
}

WL_EXPORT void
wlb_seat_pointer_enter_output(struct wlb_seat *seat,
			      struct wlb_output *output,
			      wl_fixed_t x, wl_fixed_t y)
{
	if (!seat->pointer)
		return;

	seat->pointer->x = x;
	seat->pointer->y = y;
	
	wlb_pointer_set_focus(seat->pointer, output);
}

WL_EXPORT void
wlb_seat_pointer_move_on_output(struct wlb_seat *seat, uint32_t time,
				struct wlb_output *output,
				wl_fixed_t x, wl_fixed_t y)
{
	if (!seat->pointer)
		return;

	seat->pointer->x = x;
	seat->pointer->y = y;

	wlb_pointer_set_focus(seat->pointer, output);

	wlb_pointer_send_motion(seat->pointer, time,
				wl_fixed_from_int(output->x) + x,
				wl_fixed_from_int(output->y) + y);
}

WL_EXPORT void
wlb_seat_pointer_leave_output(struct wlb_seat *seat)
{
	if (!seat->pointer)
		return;
	
	wlb_pointer_set_focus(seat->pointer, NULL);
}

WL_EXPORT void
wlb_seat_pointer_axis(struct wlb_seat *seat, uint32_t time,
		      enum wl_pointer_axis axis, wl_fixed_t value)
{
	if (!seat->pointer)
		return;
	
	wlb_pointer_send_axis(seat->pointer, time, axis, value);
}
