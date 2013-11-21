/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2010-2011 Intel Corporation
 * Copyright © 2013 Vasily Khoruzhick <anarsoul@gmail.com>
 * Copyright © 2013 Jason Ekstrand
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <pixman.h>

#include "../libwlb/libwlb.h"

struct x11_compositor {
	struct wl_display *display;
	struct wlb_compositor *compositor;

	Display *dpy;
	xcb_connection_t *conn;
	xcb_screen_t *screen;

	struct wl_array keys;

	struct {
		xcb_atom_t wm_protocols;
		xcb_atom_t wm_normal_hints;
		xcb_atom_t wm_size_hints;
		xcb_atom_t wm_delete_window;
		xcb_atom_t wm_class;
		xcb_atom_t net_wm_name;
		xcb_atom_t net_supporting_wm_check;
		xcb_atom_t net_supported;
		xcb_atom_t net_wm_icon;
		xcb_atom_t net_wm_state;
		xcb_atom_t string;
		xcb_atom_t utf8_string;
		xcb_atom_t cardinal;
		xcb_atom_t xkb_names;
	} atom;
};

struct x11_output {
	struct x11_compositor *compositor;
	struct wlb_output *output;
	int32_t width, height;

	xcb_window_t window;
	struct wl_event_source *repaint_timer;

	xcb_gc_t gc;
	xcb_shm_seg_t segment;
	pixman_image_t *hw_surface;
	int shm_id;
	void *buf;
	uint8_t depth;
};

#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])
#define F(field) offsetof(struct x11_compositor, field)

struct wm_normal_hints {
    	uint32_t flags;
	uint32_t pad[4];
	int32_t min_width, min_height;
	int32_t max_width, max_height;
    	int32_t width_inc, height_inc;
    	int32_t min_aspect_x, min_aspect_y;
    	int32_t max_aspect_x, max_aspect_y;
	int32_t base_width, base_height;
	int32_t win_gravity;
};

#define WM_NORMAL_HINTS_MIN_SIZE	16
#define WM_NORMAL_HINTS_MAX_SIZE	32

static void
x11_compositor_get_resources(struct x11_compositor *c)
{
	static const struct { const char *name; int offset; } atoms[] = {
		{ "WM_PROTOCOLS",	F(atom.wm_protocols) },
		{ "WM_NORMAL_HINTS",	F(atom.wm_normal_hints) },
		{ "WM_SIZE_HINTS",	F(atom.wm_size_hints) },
		{ "WM_DELETE_WINDOW",	F(atom.wm_delete_window) },
		{ "WM_CLASS",		F(atom.wm_class) },
		{ "_NET_WM_NAME",	F(atom.net_wm_name) },
		{ "_NET_WM_ICON",	F(atom.net_wm_icon) },
		{ "_NET_WM_STATE",	F(atom.net_wm_state) },
		{ "_NET_SUPPORTING_WM_CHECK",
					F(atom.net_supporting_wm_check) },
		{ "_NET_SUPPORTED",     F(atom.net_supported) },
		{ "STRING",		F(atom.string) },
		{ "UTF8_STRING",	F(atom.utf8_string) },
		{ "CARDINAL",		F(atom.cardinal) },
		{ "_XKB_RULES_NAMES",	F(atom.xkb_names) },
	};

	xcb_intern_atom_cookie_t cookies[ARRAY_LENGTH(atoms)];
	xcb_intern_atom_reply_t *reply;
	xcb_pixmap_t pixmap;
	xcb_gc_t gc;
	unsigned int i;
	uint8_t data[] = { 0, 0, 0, 0 };

	for (i = 0; i < ARRAY_LENGTH(atoms); i++)
		cookies[i] = xcb_intern_atom (c->conn, 0,
					      strlen(atoms[i].name),
					      atoms[i].name);

	for (i = 0; i < ARRAY_LENGTH(atoms); i++) {
		reply = xcb_intern_atom_reply (c->conn, cookies[i], NULL);
		*(xcb_atom_t *) ((char *) c + atoms[i].offset) = reply->atom;
		free(reply);
	}

	pixmap = xcb_generate_id(c->conn);
	gc = xcb_generate_id(c->conn);
	xcb_create_pixmap(c->conn, 1, pixmap, c->screen->root, 1, 1);
	xcb_create_gc(c->conn, gc, pixmap, 0, NULL);
	xcb_put_image(c->conn, XCB_IMAGE_FORMAT_XY_PIXMAP,
		      pixmap, gc, 1, 1, 0, 0, 0, 32, sizeof data, data);
#if 0
	c->null_cursor = xcb_generate_id(c->conn);
	xcb_create_cursor (c->conn, c->null_cursor,
			   pixmap, pixmap, 0, 0, 0,  0, 0, 0,  1, 1);
#endif
	xcb_free_gc(c->conn, gc);
	xcb_free_pixmap(c->conn, pixmap);
}

struct x11_compositor *
x11_compositor_create(struct wl_display *display)
{
	struct x11_compositor *c;
	xcb_screen_iterator_t siter;

	c = calloc(1, sizeof *c);
	if (!c)
		return NULL;
	
	c->display = display;
	c->compositor = wlb_compositor_create(c->display);
	if (!c->compositor)
		goto err_free;

	c->dpy = XOpenDisplay(NULL);
	if (c->dpy == NULL)
		goto err_wlb_compositor;

	c->conn = XGetXCBConnection(c->dpy);
	XSetEventQueueOwner(c->dpy, XCBOwnsEventQueue);

	if (xcb_connection_has_error(c->conn))
		goto err_xdisplay;

	siter = xcb_setup_roots_iterator(xcb_get_setup(c->conn));
	c->screen = siter.data;
	wl_array_init(&c->keys);

	x11_compositor_get_resources(c);
	//x11_compositor_get_wm_info(c);
	
	return c;

err_xdisplay:
	XCloseDisplay(c->dpy);
err_wlb_compositor:
	wlb_compositor_destroy(c->compositor);
err_free:
	free(c);
}

static xcb_visualtype_t *
find_visual_by_id(xcb_screen_t *screen,
		   xcb_visualid_t id)
{
	xcb_depth_iterator_t i;
	xcb_visualtype_iterator_t j;
	for (i = xcb_screen_allowed_depths_iterator(screen);
	     i.rem;
	     xcb_depth_next(&i)) {
		for (j = xcb_depth_visuals_iterator(i.data);
		     j.rem;
		     xcb_visualtype_next(&j)) {
			if (j.data->visual_id == id)
				return j.data;
		}
	}
	return 0;
}

static uint8_t
get_depth_of_visual(xcb_screen_t *screen,
		   xcb_visualid_t id)
{
	xcb_depth_iterator_t i;
	xcb_visualtype_iterator_t j;
	for (i = xcb_screen_allowed_depths_iterator(screen);
	     i.rem;
	     xcb_depth_next(&i)) {
		for (j = xcb_depth_visuals_iterator(i.data);
		     j.rem;
		     xcb_visualtype_next(&j)) {
			if (j.data->visual_id == id)
				return i.data->depth;
		}
	}
	return 0;
}

static int
x11_output_init_shm(struct x11_compositor *c, struct x11_output *output,
	int width, int height)
{
	xcb_screen_iterator_t iter;
	xcb_visualtype_t *visual_type;
	xcb_format_iterator_t fmt;
	xcb_void_cookie_t cookie;
	xcb_generic_error_t *err;
	const xcb_query_extension_reply_t *ext;
	int bitsperpixel = 0;
	pixman_format_code_t pixman_format;

	/* Check if SHM is available */
	ext = xcb_get_extension_data(c->conn, &xcb_shm_id);
	if (ext == NULL || !ext->present) {
		/* SHM is missing */
		fprintf(stderr, "SHM extension is not available\n");
		errno = ENOENT;
		return -1;
	}

	iter = xcb_setup_roots_iterator(xcb_get_setup(c->conn));
	visual_type = find_visual_by_id(iter.data, iter.data->root_visual);
	if (!visual_type) {
		fprintf(stderr, "Failed to lookup visual for root window\n");
		errno = ENOENT;
		return -1;
	}
	printf("Found visual, bits per value: %d, red_mask: %.8x, green_mask: %.8x, blue_mask: %.8x\n",
		visual_type->bits_per_rgb_value,
		visual_type->red_mask,
		visual_type->green_mask,
		visual_type->blue_mask);
	output->depth = get_depth_of_visual(iter.data, iter.data->root_visual);
	printf("Visual depth is %d\n", output->depth);

	for (fmt = xcb_setup_pixmap_formats_iterator(xcb_get_setup(c->conn));
	     fmt.rem;
	     xcb_format_next(&fmt)) {
		if (fmt.data->depth == output->depth) {
			bitsperpixel = fmt.data->bits_per_pixel;
			break;
		}
	}
	printf("Found format for depth %d, bpp: %d\n",
		output->depth, bitsperpixel);

	if  (bitsperpixel == 32 &&
	     visual_type->red_mask == 0xff0000 &&
	     visual_type->green_mask == 0x00ff00 &&
	     visual_type->blue_mask == 0x0000ff) {
		printf("Will use x8r8g8b8 format for SHM surfaces\n");
		pixman_format = PIXMAN_x8r8g8b8;
	} else {
		fprintf(stderr, "Can't find appropriate format for SHM pixmap\n");
		errno = ENOTSUP;
		return -1;
	}


	/* Create SHM segment and attach it */
	output->shm_id = shmget(IPC_PRIVATE, width * height * (bitsperpixel / 8), IPC_CREAT | S_IRWXU);
	if (output->shm_id == -1) {
		fprintf(stderr, "x11shm: failed to allocate SHM segment\n");
		return -1;
	}
	output->buf = shmat(output->shm_id, NULL, 0 /* read/write */);
	if (-1 == (long)output->buf) {
		fprintf(stderr, "x11shm: failed to attach SHM segment\n");
		return -1;
	}
	output->segment = xcb_generate_id(c->conn);
	cookie = xcb_shm_attach_checked(c->conn, output->segment, output->shm_id, 1);
	err = xcb_request_check(c->conn, cookie);
	if (err) {
		fprintf(stderr, "x11shm: xcb_shm_attach error %d\n", err->error_code);
		free(err);
		return -1;
	}

	shmctl(output->shm_id, IPC_RMID, NULL);

	/* Now create pixman image */
	output->hw_surface = pixman_image_create_bits(pixman_format, width, height, output->buf,
		width * (bitsperpixel / 8));

	output->gc = xcb_generate_id(c->conn);
	xcb_create_gc(c->conn, output->gc, output->window, 0, NULL);

	return 0;
}

static int
x11_output_repaint(void *data)
{
	struct x11_output *output = data;
	struct wlb_surface *surface;
	struct wl_resource *buffer;
	xcb_rectangle_t rect;
	xcb_void_cookie_t cookie;
	xcb_generic_error_t *err;
	
	if (!wlb_output_needs_repaint(output->output))
		return;
	
	wlb_output_pixman_composite(output->output, output->hw_surface);

	rect.x = 0;
	rect.y = 0;
	rect.width = output->width;
	rect.height = output->height;

	cookie = xcb_set_clip_rectangles_checked(output->compositor->conn,
						 XCB_CLIP_ORDERING_UNSORTED,
						 output->gc,
						 0, 0, 1,
						 &rect);
	err = xcb_request_check(output->compositor->conn, cookie);
	if (err != NULL) {
		fprintf(stderr, "Failed to set clip rects, err: %d\n", err->error_code);
		free(err);
	}

	cookie = xcb_shm_put_image_checked(output->compositor->conn,
					   output->window, output->gc,
					   output->width, output->height,
					   0, 0, output->width, output->height,
					   0, 0, output->depth,
					   XCB_IMAGE_FORMAT_Z_PIXMAP,
					   0, output->segment, 0);
	err = xcb_request_check(output->compositor->conn, cookie);
	if (err != NULL) {
		fprintf(stderr, "Failed to put shm image, err: %d\n", err->error_code);
		free(err);
	}

	wl_event_source_timer_update(output->repaint_timer, 10);

	return 1;
}

struct x11_output *
x11_output_create(struct x11_compositor *c, int32_t width, int32_t height)
{
	struct x11_output *output;
	xcb_screen_iterator_t iter;
	struct wm_normal_hints normal_hints;
	struct wl_event_loop *loop;

	uint32_t mask = XCB_CW_EVENT_MASK | XCB_CW_CURSOR;
	uint32_t values[2] = {
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY,
		0
	};

	output = calloc(1, sizeof *output);
	if (!output)
		return NULL;
	
	output->compositor = c;
	output->output = wlb_output_create(c->compositor,
					   width / 4, height / 4,
					   "Xwlb", "none");
	if (!output->output)
		goto err_free;
	
	output->width = width;
	output->height = height;
	wlb_output_set_mode(output->output, width, height, 60000);

	output->window = xcb_generate_id(c->conn);
	iter = xcb_setup_roots_iterator(xcb_get_setup(c->conn));
	xcb_create_window(c->conn,
			  XCB_COPY_FROM_PARENT,
			  output->window,
			  iter.data->root,
			  0, 0,
			  width, height,
			  0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  iter.data->root_visual,
			  mask, values);

	/* Don't resize me. */
	memset(&normal_hints, 0, sizeof normal_hints);
	normal_hints.flags =
		WM_NORMAL_HINTS_MAX_SIZE | WM_NORMAL_HINTS_MIN_SIZE;
	normal_hints.min_width = width;
	normal_hints.min_height = height;
	normal_hints.max_width = width;
	normal_hints.max_height = height;
	xcb_change_property(c->conn, XCB_PROP_MODE_REPLACE, output->window,
			    c->atom.wm_normal_hints,
			    c->atom.wm_size_hints, 32,
			    sizeof normal_hints / 4,
			    (uint8_t *) &normal_hints);

	xcb_map_window(c->conn, output->window);

	if (x11_output_init_shm(c, output, width, height) < 0)
		goto err_output; /* TODO: Clean up X11 stuff */

	loop = wl_display_get_event_loop(c->display);
	output->repaint_timer =
		wl_event_loop_add_timer(loop, x11_output_repaint, output);
	wl_event_source_timer_update(output->repaint_timer, 10);
	
	return output;

err_output:
	wlb_output_destroy(output->output);
err_free:
	free(output);
	return NULL;
}

int
main(int argc, char *argv[])
{
	struct x11_compositor *c;
	struct wl_display *display;

	display = wl_display_create();
	wl_display_add_socket(display, "wayland-0");

	c = x11_compositor_create(display);
	if (!c)
		return 12;
	x11_output_create(c, 1024, 720);

	wl_display_init_shm(c->display);

	wl_display_run(c->display);

	return 0;
}
