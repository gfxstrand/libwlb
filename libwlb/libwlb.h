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
#include <pixman.h>
#include <EGL/egl.h>

struct wlb_compositor;
struct wlb_output;
struct wlb_surface;
struct wlb_seat;
struct wlb_keyboard;
struct wlb_pointer;

WL_EXPORT struct wlb_compositor *
wlb_compositor_create(struct wl_display *display);
void
wlb_compositor_destroy(struct wlb_compositor *compositor);

WL_EXPORT struct wlb_output *
wlb_output_create(struct wlb_compositor *compositor, int32_t width,
		  int32_t height, const char *make, const char *model);
WL_EXPORT void
wlb_output_destroy(struct wlb_output *output);
struct wlb_output_funcs {
	int (*switch_mode)(struct wlb_output *output, void *data,
			   int32_t width, int32_t height, int32_t refresh);
	int (*place_surface)(struct wlb_output *output, void *data,
			     struct wlb_surface *surface,
			     uint32_t present_method, int32_t *x, int32_t *y,
			     uint32_t *width, uint32_t *height);
};
WL_EXPORT void
wlb_output_set_funcs_with_size(struct wlb_output *output,
			       struct wlb_output_funcs *funcs, size_t size);
static inline void
wlb_output_set_funcs(struct wlb_output *output, struct wlb_output_funcs *funcs)
{
	wlb_output_set_funcs_with_size(output, funcs, sizeof *funcs);
}
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
WL_EXPORT void
wlb_output_repaint_complete(struct wlb_output *output, uint32_t time);
WL_EXPORT struct wlb_surface *
wlb_output_surface(struct wlb_output *output);
WL_EXPORT void
wlb_output_surface_position(struct wlb_output *output, int32_t *x, int32_t *y,
			    uint32_t *width, uint32_t *height);
WL_EXPORT uint32_t
wlb_output_present_method(struct wlb_output *output);
WL_EXPORT void
wlb_output_pixman_composite(struct wlb_output *output, pixman_image_t *image);

WL_EXPORT void
wlb_surface_add_destroy_listener(struct wlb_surface *surface,
				 struct wl_listener *listener);
WL_EXPORT struct wl_listener *
wlb_surface_get_destroy_listener(struct wlb_surface *surface,
				 wl_notify_func_t notify);
WL_EXPORT void
wlb_surface_get_damage(struct wlb_surface *surface, pixman_region32_t *damage);
WL_EXPORT void
wlb_surface_reset_damage(struct wlb_surface *surface);
WL_EXPORT struct wl_resource *
wlb_surface_buffer(struct wlb_surface *surface);
WL_EXPORT enum wl_output_transform
wlb_surface_buffer_transform(struct wlb_surface *surface);
WL_EXPORT int32_t
wlb_surface_buffer_scale(struct wlb_surface *surface);

WL_EXPORT struct wlb_seat *
wlb_seat_create(struct wlb_compositor *compositor);
WL_EXPORT void
wlb_seat_destroy(struct wlb_seat *seat);

/* Returns NULL if the given seat already has a keyboard */
WL_EXPORT struct wlb_keyboard *
wlb_keyboard_create(struct wlb_seat *seat);
WL_EXPORT void
wlb_keyboard_destroy(struct wlb_keyboard *keyboard);
WL_EXPORT int
wlb_keyboard_set_keymap(struct wlb_keyboard *keyboard, const void *keymap,
			size_t len, enum wl_keyboard_keymap_format format);
WL_EXPORT void
wlb_keyboard_key(struct wlb_keyboard *keyboard, uint32_t time, uint32_t key,
		 enum wl_keyboard_key_state state);
WL_EXPORT void
wlb_keyboard_modifiers(struct wlb_keyboard *keyboard, uint32_t mods_depressed,
		       uint32_t mods_latched, uint32_t mods_locked,
		       uint32_t group);
WL_EXPORT void
wlb_keyboard_enter(struct wlb_keyboard *keyboard, const struct wl_array *keys);
WL_EXPORT void
wlb_keyboard_leave(struct wlb_keyboard *keyboard);

WL_EXPORT struct wlb_pointer *
wlb_pointer_create(struct wlb_seat *seat);
WL_EXPORT void
wlb_pointer_destroy(struct wlb_pointer *pointer);
WL_EXPORT void
wlb_pointer_motion_relative(struct wlb_pointer *pointer, uint32_t time,
			    wl_fixed_t dx, wl_fixed_t dy);
WL_EXPORT void
wlb_pointer_motion_absolute(struct wlb_pointer *pointer, uint32_t time,
			    wl_fixed_t x, wl_fixed_t y);
WL_EXPORT void
wlb_pointer_button(struct wlb_pointer *pointer, uint32_t time, uint32_t button,
		   enum wl_pointer_button_state state);
WL_EXPORT void
wlb_pointer_axis(struct wlb_pointer *pointer, uint32_t time,
		 enum wl_pointer_axis axis, wl_fixed_t value);
WL_EXPORT void
wlb_pointer_enter_output(struct wlb_pointer *pointer, struct wlb_output *output,
			 wl_fixed_t x, wl_fixed_t y);
WL_EXPORT void
wlb_pointer_move_on_output(struct wlb_pointer *pointer, uint32_t time,
			   struct wlb_output *output,
			   wl_fixed_t x, wl_fixed_t y);
WL_EXPORT void
wlb_pointer_leave_output(struct wlb_pointer *pointer);

struct wlb_gles2_renderer;
WL_EXPORT struct wlb_gles2_renderer *
wlb_gles2_renderer_create(struct wlb_compositor *c);
WL_EXPORT struct wlb_gles2_renderer *
wlb_gles2_renderer_create_for_egl(struct wlb_compositor *c,
				  EGLDisplay display, EGLConfig *config);
WL_EXPORT void
wlb_gles2_renderer_destroy(struct wlb_gles2_renderer *renderer);
WL_EXPORT void
wlb_gles2_renderer_add_egl_output(struct wlb_gles2_renderer *renderer,
				  struct wlb_output *output,
				  EGLNativeWindowType window);
WL_EXPORT void
wlb_gles2_renderer_repaint_output(struct wlb_gles2_renderer *renderer,
				  struct wlb_output *output);

#endif /* !defined LIBWLB_LIBWLB_H */
