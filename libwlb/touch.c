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
#include <errno.h>

WL_EXPORT struct wlb_touch *
wlb_touch_create(struct wlb_seat *seat)
{
	struct wlb_touch *touch;

	touch = zalloc(sizeof *touch);
	if (!touch)
		return NULL;
	
	touch->seat = seat;

	wl_list_init(&touch->resource_list);
	wl_list_init(&touch->finger_list);

	seat->touch = touch;

	return touch;
}

WL_EXPORT void
wlb_touch_destroy(struct wlb_touch *touch)
{
	struct wl_resource *resource, *rnext;

	wl_resource_for_each_safe(resource, rnext, &touch->resource_list)
		wl_resource_destroy(resource);
	
	touch->seat->touch = NULL;

	free(touch);
}

static void
touch_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

struct wl_touch_interface touch_interface = {
	touch_release
};

static void
unlink_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

void
wlb_touch_create_resource(struct wlb_touch *touch,
			  struct wl_client *client, uint32_t id)
{
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_touch_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &touch_interface,
				       touch, unlink_resource);

	wl_list_insert(&touch->resource_list, wl_resource_get_link(resource));
}

WL_EXPORT int
wlb_touch_down_on_output(struct wlb_touch *touch, uint32_t time, int32_t id,
			 struct wlb_output *output, wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_finger *finger;
	struct wl_resource *resource;
	wl_fixed_t sx, sy;
	uint32_t serial;

	wlb_output_to_surface_coords(output, x, y, &sx, &sy);

	if (!output->surface.surface)
		return 0;

	if (sx < 0 || sy < 0 ||
	    sx >= wl_fixed_from_int(output->surface.position.width) ||
	    sy >= wl_fixed_from_int(output->surface.position.height))
		return 0;
	
	finger = malloc(sizeof *finger);
	if (!finger)
		return -1;

	wl_list_insert(&touch->finger_list, &finger->link);
	finger->id = id;
	finger->output = output;
	finger->focus = output->surface.surface;
	wl_resource_add_destroy_listener(output->surface.surface->resource,
					 &finger->focus_destroy);
	finger->sx = sx;
	finger->sy = sy;

	serial = wl_display_next_serial(touch->seat->compositor->display);

	wl_resource_for_each(resource, &touch->resource_list)
		wl_touch_send_down(resource, serial, time,
				   output->surface.surface->resource,
				   id, sx, sy);
	return 0;
}

static struct wlb_finger *
wlb_touch_find_finger(struct wlb_touch *touch, int32_t id)
{
	struct wlb_finger *finger;

	wl_list_for_each(finger, &touch->finger_list, link)
		if (finger->id == id)
			return finger;
	
	return NULL;
}

WL_EXPORT int
wlb_touch_move_on_output(struct wlb_touch *touch, int32_t id,
			 struct wlb_output *output, wl_fixed_t x, wl_fixed_t y)
{
	struct wlb_finger *finger;
	struct wl_resource *resource;
	wl_fixed_t sx, sy;

	finger = wlb_touch_find_finger(touch, id);
	if (!finger)
		return 0;

	if(finger->output != output) {
		errno = EINVAL;
		return -1;
	}

	wlb_output_to_surface_coords(output, x, y, &finger->sx, &finger->sy);

	/* The data will be sent to the client in wlb_touch_finish_frame */

	return 0;
}

WL_EXPORT void
wlb_touch_finish_frame(struct wlb_touch *touch, uint32_t time)
{
	struct wlb_finger *finger;
	struct wl_resource *resource;

	wl_resource_for_each(resource, &touch->resource_list) {
		wl_list_for_each(finger, &touch->finger_list, link)
			wl_touch_send_motion(resource, time, finger->id,
					     finger->sx, finger->sy);
		wl_touch_send_frame(resource);
	}
}

WL_EXPORT void
wlb_touch_up(struct wlb_touch *touch, uint32_t time, int32_t id)
{
	struct wlb_finger *finger;
	struct wl_resource *resource;
	uint32_t serial;

	finger = wlb_touch_find_finger(touch, id);
	if (!finger)
		return;

	serial = wl_display_next_serial(touch->seat->compositor->display);

	wl_resource_for_each(resource, &touch->resource_list)
		wl_touch_send_up(resource, serial, time, id);
	
	wl_list_remove(&finger->link);
	free(finger);
}

WL_EXPORT void
wlb_touch_cancel(struct wlb_touch *touch)
{
	struct wl_resource *resource;
	struct wlb_finger *finger, *fnext;

	wl_resource_for_each(resource, &touch->resource_list)
		wl_touch_send_cancel(resource);
	
	wl_list_for_each_safe(finger, fnext, &touch->finger_list, link) {
		wl_list_remove(&finger->link);
		free(finger);
	}
}

