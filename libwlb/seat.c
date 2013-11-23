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

struct wlb_seat *
wlb_seat_create(struct wlb_compositor *compositor, uint32_t capabilities)
{
	struct wlb_seat *seat;

	seat = zalloc(sizeof *seat);
	if (!seat)
		return NULL;
	
	seat->compositor = compositor;
	seat->capabilities = capabilities;

	wl_list_insert(&compositor->seat_list, &seat->compositor_link);

	return seat;
}

void
wlb_seat_destroy(struct wlb_seat *seat)
{
	wl_list_remove(&seat->compositor_link);

	free(seat);
}

void
wlb_seat_pointer_enter(struct wlb_seat *seat, wl_fixed_t x, wl_fixed_t y)
{
	/* TODO */
}

void
wlb_seat_pointer_motion_relative(struct wlb_seat *seat,
				 wl_fixed_t dx, wl_fixed_t dy)
{
	/* TODO */
}

void
wlb_seat_pointer_motion_absolute(struct wlb_seat *seat,
				 wl_fixed_t dx, wl_fixed_t dy)
{
	/* TODO */
}

void
wlb_seat_pointer_motion_from_output(struct wlb_seat *seat,
				    struct wlb_output *output,
				    wl_fixed_t x, wl_fixed_t y)
{
	/* TODO */
}

void
wlb_seat_pointer_leave(struct wlb_seat *seat)
{
	/* TODO */
}
