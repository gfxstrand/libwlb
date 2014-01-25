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
pointer_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_pointer *pointer;

	pointer = wl_container_of(listener, pointer, surface_destroy_listener);

	wlb_pointer_set_focus(pointer, NULL);
}

static void
pointer_output_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_pointer *pointer;

	pointer = wl_container_of(listener, pointer, output_destroy_listener);

	wlb_pointer_set_focus(pointer, NULL);
}

WL_EXPORT struct wlb_pointer *
wlb_pointer_create(struct wlb_seat *seat)
{
	struct wlb_pointer *pointer;

	pointer = zalloc(sizeof *pointer);
	if (!pointer)
		return NULL;
	
	pointer->seat = seat;

	wl_list_init(&pointer->resource_list);
	
	pointer->surface_destroy_listener.notify = pointer_surface_destroyed;
	pointer->output_destroy_listener.notify = pointer_output_destroyed;

	seat->pointer = pointer;

	return pointer;
}

WL_EXPORT void
wlb_pointer_destroy(struct wlb_pointer *pointer)
{
	struct wl_resource *resource, *rnext;

	wl_resource_for_each_safe(resource, rnext, &pointer->resource_list)
		wl_resource_destroy(resource);

	if (pointer->focus) {
		wl_list_remove(&pointer->surface_destroy_listener.link);
		wl_list_remove(&pointer->output_destroy_listener.link);
	}

	pointer->seat->pointer = NULL;

	free(pointer);
}

static void
pointer_set_cursor(struct wl_client *client, struct wl_resource *resource,
		   uint32_t serial, struct wl_resource *surface,
		   int32_t hotspot_x, int32_t hotspot_y)
{ }

static void
pointer_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

struct wl_pointer_interface pointer_interface = {
	pointer_set_cursor,
	pointer_release
};

static void
unlink_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

void
wlb_pointer_create_resource(struct wlb_pointer *pointer,
			    struct wl_client *client, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_pointer_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &pointer_interface,
				       pointer, unlink_resource);

	wl_list_insert(&pointer->resource_list, wl_resource_get_link(resource));
}

void
wlb_pointer_set_focus(struct wlb_pointer *pointer, struct wlb_output *output)
{
	struct wl_resource *resource;
	uint32_t serial;
	wl_fixed_t sx, sy;

	if ((output == NULL && pointer->focus == NULL) ||
	    (output != NULL && output == pointer->focus &&
	     output->surface.surface == pointer->focus_surface))
		return;

	serial = wl_display_next_serial(pointer->seat->compositor->display);

	if (pointer->focus) {
		wl_resource_for_each(resource, &pointer->resource_list)
			wl_pointer_send_leave(resource, serial,
					      pointer->focus_surface->resource);

		wl_list_remove(&pointer->surface_destroy_listener.link);
		wl_list_remove(&pointer->output_destroy_listener.link);
	}

	pointer->focus = NULL;
	pointer->focus_surface = NULL;

	if (output && output->surface.surface) {
		pointer->focus = output;
		pointer->focus_surface = output->surface.surface;
		wl_signal_add(&output->destroy_signal,
			      &pointer->output_destroy_listener);
		wl_resource_add_destroy_listener(pointer->focus_surface->resource,
						 &pointer->surface_destroy_listener);

		sx = pointer->x - wl_fixed_from_int(output->x) -
		     wl_fixed_from_int(output->surface.position.x);
		sy = pointer->y - wl_fixed_from_int(output->y) -
		     wl_fixed_from_int(output->surface.position.y);

		wl_resource_for_each(resource, &pointer->resource_list)
			wl_pointer_send_enter(resource, serial,
					      pointer->focus_surface->resource,
					      sx, sy);
	}
}

void
wlb_pointer_update_focus(struct wlb_pointer *pointer)
{
	struct wlb_output *output;
	int32_t ix, iy;

	ix = wl_fixed_to_int(pointer->x);
	iy = wl_fixed_to_int(pointer->y);

	/* Try and keep the focus if we can */
	output = pointer->focus;
	if (ix < output->x || iy < output->y ||
	    ix >= output->x + output->current_mode->width ||
	    iy >= output->y + output->current_mode->height)
		output = wlb_output_find_with_surface(pointer->seat->compositor,
						      pointer->x, pointer->y);
	
	wlb_pointer_set_focus(pointer, output);
}

static void
pointer_send_motion(struct wlb_pointer *pointer, uint32_t time)
{
	struct wl_resource *resource;
	wl_fixed_t sx, sy;

	if (!pointer->focus)
		return;

	sx = pointer->x - wl_fixed_from_int(pointer->focus->x) -
	     wl_fixed_from_int(pointer->focus->surface.position.x);
	sy = pointer->y - wl_fixed_from_int(pointer->focus->y) -
	     wl_fixed_from_int(pointer->focus->surface.position.y);

	wl_resource_for_each(resource, &pointer->resource_list)
		wl_pointer_send_motion(resource, time, sx, sy);
}

WL_EXPORT void
wlb_pointer_motion_relative(struct wlb_pointer *pointer, uint32_t time,
			    wl_fixed_t dx, wl_fixed_t dy)
{
	wlb_pointer_motion_absolute(pointer, time,
				    pointer->x + dx, pointer->y + dy);
}

WL_EXPORT void
wlb_pointer_motion_absolute(struct wlb_pointer *pointer, uint32_t time,
			    wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_output *output;

	pointer->x = x;
	pointer->y = y;
	
	if (pointer->button_count > 0) {
		output = wlb_output_find_with_surface(pointer->seat->compositor, x, y);
		if (pointer->focus != output)
			wlb_pointer_set_focus(pointer, output);
	}

	pointer_send_motion(pointer, time);
}

WL_EXPORT void
wlb_pointer_button(struct wlb_pointer *pointer, uint32_t time,
		   uint32_t button, enum wl_pointer_button_state state)
{
	struct wl_resource *resource;
	uint32_t serial;

	switch (state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		pointer->button_count++;
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		pointer->button_count--;
		break;
	}

	serial = wl_display_next_serial(pointer->seat->compositor->display);
	
	wl_resource_for_each(resource, &pointer->resource_list)
		wl_pointer_send_button(resource, serial, time, button, state);
}

WL_EXPORT void
wlb_pointer_axis(struct wlb_pointer *pointer, uint32_t time,
		 enum wl_pointer_axis axis, wl_fixed_t value)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, &pointer->resource_list)
		wl_pointer_send_axis(resource, time, axis, value);
}

WL_EXPORT void
wlb_pointer_enter_output(struct wlb_pointer *pointer, struct wlb_output *output,
			 wl_fixed_t x, wl_fixed_t y)
{
	pointer->x = x + wl_fixed_from_int(output->x);
	pointer->y = y + wl_fixed_from_int(output->y);
	
	wlb_pointer_set_focus(pointer, output);
}

WL_EXPORT void
wlb_pointer_move_on_output(struct wlb_pointer *pointer, uint32_t time,
			   struct wlb_output *output,
			   wl_fixed_t x, wl_fixed_t y)
{
	pointer->x = x + wl_fixed_from_int(output->x);
	pointer->y = y + wl_fixed_from_int(output->y);

	wlb_pointer_set_focus(pointer, output);

	pointer_send_motion(pointer, time);
}

WL_EXPORT void
wlb_pointer_leave_output(struct wlb_pointer *pointer)
{
	wlb_pointer_set_focus(pointer, NULL);
}
