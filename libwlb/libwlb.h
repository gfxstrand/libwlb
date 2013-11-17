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

#ifndef LIBWLB_LIBWLB_H
#define LIBWLB_LIBWLB_H

#include <wayland-server.h>

struct wlb_compositor;
struct wlb_output;
struct wlb_surface;
struct wlb_seat;

WL_EXPORT struct wlb_compositor *
wlb_compositor_create(struct wl_display *display);
void
wlb_compositor_destroy(struct wlb_compositor *compositor);

WL_EXPORT struct wlb_output *
wlb_output_create(struct wlb_compositor *compositor, int32_t width,
		  int32_t height, const char *make, const char *model);
WL_EXPORT void
wlb_output_destroy(struct wlb_output *output);
WL_EXPORT void
wlb_output_set_transform(struct wlb_output *output,
			 enum wl_output_transform transform);
WL_EXPORT void
wlb_output_set_subpixel(struct wlb_output *output,
			enum wl_output_subpixel subpixel);
WL_EXPORT void
wlb_output_add_mode(struct wlb_output *output,
		    int32_t width, int32_t height, int32_t refresh);
WL_EXPORT void
wlb_output_set_mode(struct wlb_output *output,
		    int32_t width, int32_t height, int32_t refresh);
WL_EXPORT void
wlb_output_set_preferred_mode(struct wlb_output *output,
			      int32_t width, int32_t height, int32_t refresh);
WL_EXPORT int
wlb_output_needs_repaint(struct wlb_output *output);
WL_EXPORT struct wlb_surface *
wlb_output_surface(struct wlb_output *output);
WL_EXPORT uint32_t
wlb_output_presentation_mode(struct wlb_output *output);

WL_EXPORT struct wl_resource *
wlb_surface_buffer(struct wlb_surface *surface);
WL_EXPORT enum wl_surface_buffer_transform
wlb_surface_buffer_transform(struct wlb_surface *surface);
WL_EXPORT enum wl_surface_buffer_transform
wlb_surface_buffer_transform(struct wlb_surface *surface);

struct wlb_seat *
wlb_seat_create(struct wlb_compositor *compositor, uint32_t capabilities);

void
wlb_seat_keyboard_key(struct wlb_seat *seat, uint32_t time, uint32_t key,
		      enum wl_keyboard_key_state state);
void
wlb_seat_keyboard_modifiers(struct wlb_seat *seat, uint32_t serial);

void
wlb_seat_pointer_enter(struct wlb_seat, wl_fixed_t x, wl_fixed_t y);
void
wlb_seat_pointer_motion_absolute(struct wlb_seat, wl_fixed_t x, wl_fixed_t y);
void
wlb_seat_pointer_motion_relative(struct wlb_seat, wl_fixed_t dx, wl_fixed_t dy);
void
wlb_seat_pointer_leave(struct wlb_seat);

void
wlb_seat_touch_down(struct wlb_seat, uint32_t id, wl_fixed_t x, wl_fixed_t y);
void
wlb_seat_touch_motion(struct wlb_seat, uint32_t id, wl_fixed_t x, wl_fixed_t y);
void
wlb_seat_touch_up(struct wlb_seat, uint32_t id);
void
wlb_seat_touch_cancel(struct wlb_seat);

#endif /* !defined LIBWLB_LIBWLB_H */
