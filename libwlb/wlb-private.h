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

#ifndef LIBWLB_WLB_PRIVATE_H
#define LIBWLB_WLB_PRIVATE_H

#include "libwlb.h"
#include "fullscreen-shell-server-protocol.h"

#include <pixman.h>

struct wlb_compositor {
	struct wl_display *display;

	struct wl_list output_list;
	struct wl_list seat_list;
};

struct wlb_region {
	struct wl_resource *resource;
	pixman_region32_t region;
};

struct wlb_output_mode {
	struct wl_list link;

	int32_t width;
	int32_t height;
	int32_t refresh;
};

struct wlb_output {
	struct wlb_compositor *compositor;
	struct wl_list compositor_link;

	struct wl_global *global;
	struct wl_list resource_list;

	struct {
		char *make;
		char *model;
		int32_t width;
		int32_t height;

		enum wl_output_subpixel subpixel;
		enum wl_output_transform transform;
	} physical;

	struct wl_list mode_list;
	struct wlb_output_mode *current_mode;
	struct wlb_output_mode *preferred_mode;

	int32_t x, y;

	struct {
		struct wlb_surface *surface;
		struct wl_list link;
		enum wl_fullscreen_shell_present_method present_method;

		pixman_rectangle32_t position;
	} surface;
};

void
wlb_output_present_surface(struct wlb_output *output,
			   struct wlb_surface *surface,
			   enum wl_fullscreen_shell_present_method method,
			   int32_t framerate);
void
wlb_output_recompute_surface_position(struct wlb_output *output);
void
wlb_output_get_matrix(struct wlb_output *output,
		      pixman_transform_t *transform);

struct wlb_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

struct wlb_surface {
	struct wl_resource *resource;

	struct wl_list output_list;

	struct {
		struct wl_resource *buffer;
		struct wl_listener buffer_destroy_listener;

		pixman_region32_t damage;
		pixman_region32_t input_region;

		struct wl_list frame_callbacks;
	} pending;

	struct wl_resource *buffer;
	struct wl_listener buffer_destroy_listener;
	int32_t width, height;

	pixman_region32_t damage;
	pixman_region32_t input_region;

	struct wl_list frame_callbacks;
};

struct wlb_surface *
wlb_surface_create(struct wl_client *client, uint32_t id);
void
wlb_surface_destroy(struct wlb_surface *surface);

void *zalloc(size_t size);

#endif /* !defined LIBWLB_WLB_PRIVATE_H */
