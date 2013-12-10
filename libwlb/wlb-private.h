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
#include "config.h"
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

/*! 3x3 matrix storred column-major
 *
 * Indicies are: 
 * [ 0  3  6 ]
 * [ 1  4  7 ]
 * [ 2  5  8 ]
 */
struct wlb_matrix {
	float d[9];
};

void wlb_matrix_init(struct wlb_matrix *matrix);
/*! Computes C = AB */
void
wlb_matrix_mult(struct wlb_matrix *dest,
		const struct wlb_matrix *A, const struct wlb_matrix *B);
void
wlb_matrix_translate(struct wlb_matrix *dest,
		     const struct wlb_matrix *src, float dx, float dy);
void
wlb_matrix_rotate(struct wlb_matrix *dest,
		  const struct wlb_matrix *src, float theta);
void
wlb_matrix_scale(struct wlb_matrix *dest,
		 const struct wlb_matrix *src, float sx, float sy);
void
wlb_matrix_ortho(struct wlb_matrix *dest, float l, float r, float t, float b);

struct wlb_output_mode {
	struct wl_list link;

	int32_t width;
	int32_t height;
	int32_t refresh;
};

struct wlb_output {
	struct wlb_compositor *compositor;
	struct wl_list compositor_link;
	struct wl_signal destroy_signal;

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
void
wlb_output_transform_matrix(struct wlb_output *output, struct wlb_matrix *mat);
void
wlb_output_to_surface_coords(struct wlb_output *output,
			     wl_fixed_t x, wl_fixed_t y,
			     wl_fixed_t *sx, wl_fixed_t *sy);
struct wlb_output *
wlb_output_find(struct wlb_compositor *compositor, wl_fixed_t x, wl_fixed_t y);
struct wlb_output *
wlb_output_find_with_surface(struct wlb_compositor *compositor,
			     wl_fixed_t x, wl_fixed_t y);

struct wlb_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

struct wlb_surface {
	struct wl_resource *resource;

	struct wl_list output_list;
	struct wlb_output *primary_output;

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
void
wlb_surface_compute_primary_output(struct wlb_surface *surface);
void
wlb_surface_post_frame_callbacks(struct wlb_surface *surface, uint32_t time);

struct wlb_pointer {
	struct wlb_seat *seat;

	wl_fixed_t x, y;

	struct wl_list resource_list;

	int32_t button_count;

	struct wlb_output *focus;
	struct wlb_surface *focus_surface;
	struct wl_listener surface_destroy_listener;
	struct wl_listener output_destroy_listener;
};

struct wlb_pointer *
wlb_pointer_create(struct wlb_seat *seat);
void
wlb_pointer_create_resource(struct wlb_pointer *pointer,
			    struct wl_client *client, uint32_t id);
void
wlb_pointer_destroy(struct wlb_pointer *pointer);
void
wlb_pointer_set_focus(struct wlb_pointer *pointer, struct wlb_output *output);
void
wlb_pointer_update_focus(struct wlb_pointer *pointer);
void
wlb_pointer_send_motion(struct wlb_pointer *pointer, uint32_t time,
			wl_fixed_t x, wl_fixed_t y);
void
wlb_pointer_send_button(struct wlb_pointer *pointer, uint32_t time,
			uint32_t button, enum wl_pointer_button_state state);
void
wlb_pointer_send_axis(struct wlb_pointer *pointer, uint32_t time,
		      enum wl_pointer_axis axis, wl_fixed_t value);

struct wlb_keyboard {
	struct wlb_seat *seat;

	struct wl_list resource_list;

	struct wlb_surface *focus;
	struct wl_listener surface_destroy_listener;

	struct wl_array keys;

	struct {
		int fd;
		void *data;
		size_t size;
		enum wl_keyboard_keymap_format format;
	} keymap;
};

struct wlb_keyboard *
wlb_keyboard_create(struct wlb_seat *seat);
void
wlb_keyboard_destroy(struct wlb_keyboard *keyboard);
void
wlb_keyboard_create_resource(struct wlb_keyboard *keyboard,
			     struct wl_client *client, uint32_t id);
void
wlb_keyboard_set_focus(struct wlb_keyboard *keyboard,
		       struct wlb_surface *focus);
int
wlb_keyboard_set_keymap(struct wlb_keyboard *keyboard, const void *data,
			size_t size, enum wl_keyboard_keymap_format format);
void
wlb_keyboard_key(struct wlb_keyboard *keyboard, uint32_t time,
		 uint32_t key, enum wl_keyboard_key_state state);
void
wlb_keyboard_modifiers(struct wlb_keyboard *keyboard, uint32_t mods_depressed,
		       uint32_t mods_latched, uint32_t mods_locked,
		       uint32_t group);

struct wlb_seat {
	struct wlb_compositor *compositor;
	struct wl_list compositor_link;
	struct wl_global *global;

	struct wl_list resource_list;

	struct wlb_pointer *pointer;
	struct wlb_keyboard *keyboard;
};

void
wlb_seat_send_capabilities(struct wlb_seat *seat);

int wlb_util_create_tmpfile(size_t size);

enum wlb_log_level {
	WLB_LOG_LEVEL_ERROR,
	WLG_LOG_LEVEL_WARNING,
	WLB_LOG_LEVEL_DEBUG,
};

int wlb_log(enum wlb_log_level level, const char *format, ...);

#define wlb_error(...) wlb_log(WLB_LOG_LEVEL_ERROR, __VA_ARGS__)
#define wlb_warn(...) wlb_log(WLB_LOG_LEVEL_WARNING, __VA_ARGS__)

#ifdef NDEBUG
#	define wlb_debug(...) 0
#else
#	define wlb_debug(...) wlb_log(WLB_LOG_LEVEL_DEBUG, __VA_ARGS__)
#endif

void *zalloc(size_t size);

#endif /* !defined LIBWLB_WLB_PRIVATE_H */
