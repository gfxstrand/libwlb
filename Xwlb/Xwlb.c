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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <linux/input.h>

#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xkb.h>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <xkbcommon/xkbcommon.h>

#include <pixman.h>

#include "config.h"
#include "../libwlb/libwlb.h"

#define DEFAULT_AXIS_STEP_DISTANCE wl_fixed_from_int(10)

struct x11_compositor {
	struct wl_display *display;
	struct wlb_compositor *compositor;

	Display *dpy;
	xcb_connection_t *conn;
	xcb_screen_t *screen;

	struct wl_array keys;
	struct wl_event_source *xcb_source;
	unsigned int has_xkb;
	uint8_t xkb_event_base;

	struct {
		struct xkb_context *context;
		struct xkb_keymap *keymap;
		struct xkb_state *state;

		uint32_t mods_depressed;
		uint32_t mods_latched;
		uint32_t mods_locked;

		uint32_t mod_shift;
		uint32_t mod_caps;
		uint32_t mod_ctrl;
		uint32_t mod_alt;
		uint32_t mod_mod2;
		uint32_t mod_mod3;
		uint32_t mod_super;
		uint32_t mod_mod5;
	} xkb;

	struct wl_list output_list;
	struct wlb_seat *seat;
	struct wlb_pointer *pointer;
	struct wlb_keyboard *keyboard;

	struct {
		xkb_mod_index_t shift_mod;
		xkb_mod_index_t caps_mod;
		xkb_mod_index_t ctrl_mod;
		xkb_mod_index_t alt_mod;
		xkb_mod_index_t mod2_mod;
		xkb_mod_index_t mod3_mod;
		xkb_mod_index_t super_mod;
		xkb_mod_index_t mod5_mod;
		xkb_led_index_t num_led;
		xkb_led_index_t caps_led;
		xkb_led_index_t scroll_led;
	} xkb_info;

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
	struct wl_list compositor_link;
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

static struct xkb_keymap *
x11_compositor_get_keymap(struct x11_compositor *c)
{
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
	struct xkb_rule_names names;
	struct xkb_keymap *ret;
	const char *value_all, *value_part;
	int length_all, length_part;

	memset(&names, 0, sizeof(names));

	cookie = xcb_get_property(c->conn, 0, c->screen->root,
				  c->atom.xkb_names, c->atom.string, 0, 1024);
	reply = xcb_get_property_reply(c->conn, cookie, NULL);
	if (reply == NULL)
		return NULL;

	value_all = xcb_get_property_value(reply);
	length_all = xcb_get_property_value_length(reply);
	value_part = value_all;

#define copy_prop_value(to) \
	length_part = strlen(value_part); \
	if (value_part + length_part < (value_all + length_all) && \
	    length_part > 0) \
		names.to = value_part; \
	value_part += length_part + 1;

	copy_prop_value(rules);
	copy_prop_value(model);
	copy_prop_value(layout);
	copy_prop_value(variant);
	copy_prop_value(options);
#undef copy_prop_value

	ret = xkb_map_new_from_names(c->xkb.context, &names, 0);

	free(reply);
	return ret;
}

static void
x11_compositor_get_xkb_info(struct x11_compositor *c)
{
	c->xkb.mod_shift = xkb_map_mod_get_index(c->xkb.keymap,
						 XKB_MOD_NAME_SHIFT);
	c->xkb.mod_caps = xkb_map_mod_get_index(c->xkb.keymap,
						XKB_MOD_NAME_CAPS);
	c->xkb.mod_ctrl = xkb_map_mod_get_index(c->xkb.keymap,
						XKB_MOD_NAME_CTRL);
	c->xkb.mod_alt = xkb_map_mod_get_index(c->xkb.keymap,
					       XKB_MOD_NAME_ALT);
	c->xkb.mod_mod2 = xkb_map_mod_get_index(c->xkb.keymap, "Mod2");
	c->xkb.mod_mod3 = xkb_map_mod_get_index(c->xkb.keymap, "Mod3");
	c->xkb.mod_super = xkb_map_mod_get_index(c->xkb.keymap,
						 XKB_MOD_NAME_LOGO);
	c->xkb.mod_mod5 = xkb_map_mod_get_index(c->xkb.keymap, "Mod5");
}

static uint32_t
get_xkb_mod_mask(struct x11_compositor *c, uint32_t in)
{
	uint32_t ret = 0;

	if ((in & ShiftMask) && c->xkb.mod_shift != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_shift);
	if ((in & LockMask) && c->xkb.mod_caps != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_caps);
	if ((in & ControlMask) && c->xkb.mod_ctrl != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_ctrl);
	if ((in & Mod1Mask) && c->xkb.mod_alt != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_alt);
	if ((in & Mod2Mask) && c->xkb.mod_mod2 != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_mod2);
	if ((in & Mod3Mask) && c->xkb.mod_mod3 != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_mod3);
	if ((in & Mod4Mask) && c->xkb.mod_super != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_super);
	if ((in & Mod5Mask) && c->xkb.mod_mod5 != XKB_MOD_INVALID)
		ret |= (1 << c->xkb.mod_mod5);

	return ret;
}

static void
x11_compositor_setup_xkb(struct x11_compositor *c)
{
#ifndef HAVE_XCB_XKB
	fprintf(stderr, "XCB-XKB not available during build\n");
	c->has_xkb = 0;
	c->xkb_event_base = 0;
	return;
#else
	const xcb_query_extension_reply_t *ext;
	xcb_generic_error_t *error;
	xcb_void_cookie_t select;
	xcb_xkb_use_extension_cookie_t use_ext;
	xcb_xkb_use_extension_reply_t *use_ext_reply;
	xcb_xkb_per_client_flags_cookie_t pcf;
	xcb_xkb_per_client_flags_reply_t *pcf_reply;
	xcb_xkb_get_state_cookie_t state;
	xcb_xkb_get_state_reply_t *state_reply;
	uint32_t values[1] = { XCB_EVENT_MASK_PROPERTY_CHANGE };

	c->has_xkb = 0;
	c->xkb_event_base = 0;

	ext = xcb_get_extension_data(c->conn, &xcb_xkb_id);
	if (!ext) {
		fprintf(stderr,
			"XKB extension not available on host X11 server\n");
		return;
	}
	c->xkb_event_base = ext->first_event;

	select = xcb_xkb_select_events_checked(c->conn,
					       XCB_XKB_ID_USE_CORE_KBD,
					       XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
					       0,
					       XCB_XKB_EVENT_TYPE_STATE_NOTIFY,
					       0,
					       0,
					       NULL);
	error = xcb_request_check(c->conn, select);
	if (error) {
		fprintf(stderr,
			"error: failed to select for XKB state events\n");
		free(error);
		return;
	}

	use_ext = xcb_xkb_use_extension(c->conn,
					XCB_XKB_MAJOR_VERSION,
					XCB_XKB_MINOR_VERSION);
	use_ext_reply = xcb_xkb_use_extension_reply(c->conn, use_ext, NULL);
	if (!use_ext_reply) {
		fprintf(stderr, "couldn't start using XKB extension\n");
		return;
	}

	if (!use_ext_reply->supported) {
		fprintf(stderr,
			"XKB extension version on the server is too old "
			"(want %d.%d, has %d.%d)\n",
			XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION,
			use_ext_reply->serverMajor, use_ext_reply->serverMinor);
		free(use_ext_reply);
		return;
	}
	free(use_ext_reply);

	pcf = xcb_xkb_per_client_flags(c->conn,
				       XCB_XKB_ID_USE_CORE_KBD,
				       XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
				       XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
				       0,
				       0,
				       0);
	pcf_reply = xcb_xkb_per_client_flags_reply(c->conn, pcf, NULL);
	if (!pcf_reply ||
	    !(pcf_reply->value & XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT)) {
		fprintf(stderr, "failed to set XKB per-client flags, not using "
			"detectable repeat\n");
		free(pcf_reply);
		return;
	}
	free(pcf_reply);

	state = xcb_xkb_get_state(c->conn, XCB_XKB_ID_USE_CORE_KBD);
	state_reply = xcb_xkb_get_state_reply(c->conn, state, NULL);
	if (!state_reply) {
		fprintf(stderr, "failed to get initial XKB state\n");
		return;
	}

	xkb_state_update_mask(c->xkb.state,
			      get_xkb_mod_mask(c, state_reply->baseMods),
			      get_xkb_mod_mask(c, state_reply->latchedMods),
			      get_xkb_mod_mask(c, state_reply->lockedMods),
			      0,
			      0,
			      state_reply->group);

	free(state_reply);

	xcb_change_window_attributes(c->conn, c->screen->root,
				     XCB_CW_EVENT_MASK, values);

	c->has_xkb = 1;
#endif
}

#ifdef HAVE_XCB_XKB
static void
update_xkb_keymap(struct x11_compositor *c)
{
	struct xkb_keymap *keymap;
	char *keymap_str;

	keymap = x11_compositor_get_keymap(c);
	if (!keymap) {
		fprintf(stderr, "failed to get XKB keymap\n");
		return;
	}
	xkb_keymap_unref(c->xkb.keymap);
	c->xkb.keymap = keymap;
	keymap_str = xkb_keymap_get_as_string(c->xkb.keymap,
					      XKB_KEYMAP_FORMAT_TEXT_V1);
	wlb_keyboard_set_keymap(c->keyboard, keymap_str, strlen(keymap_str),
				WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
	free(keymap_str);
}
#endif

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

static int
x11_input_create(struct x11_compositor *c)
{
	struct xkb_keymap *keymap;
	char *keymap_str;

	c->seat = wlb_seat_create(c->compositor);
	c->pointer = wlb_pointer_create(c->seat);
	c->keyboard = wlb_keyboard_create(c->seat);

	c->xkb.context = xkb_context_new(0);

	c->xkb.keymap = x11_compositor_get_keymap(c);
	keymap_str = xkb_keymap_get_as_string(c->xkb.keymap,
					      XKB_KEYMAP_FORMAT_TEXT_V1);
	wlb_keyboard_set_keymap(c->keyboard, keymap_str, strlen(keymap_str),
				WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);
	free(keymap_str);

	c->xkb.state = xkb_state_new(c->xkb.keymap);
	x11_compositor_setup_xkb(c);

	return 0;
}

static void
x11_input_destroy(struct x11_compositor *c)
{
	xkb_state_unref(c->xkb.state);
	xkb_keymap_unref(c->xkb.keymap);
	xkb_context_unref(c->xkb.context);
	memset(&c->xkb, 0, sizeof(c->xkb));

	wlb_seat_destroy(c->seat);
}

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

uint32_t
x11_compositor_get_time()
{
       struct timeval tv;
       gettimeofday(&tv, NULL);
       return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static struct x11_output *
x11_compositor_find_output(struct x11_compositor *c, xcb_window_t window)
{
	struct x11_output *output;

	wl_list_for_each(output, &c->output_list, compositor_link) {
		if (output->window == window)
			return output;
	}

	assert(0);
}

#ifdef HAVE_XCB_XKB
static void
update_xkb_state(struct x11_compositor *c, xcb_xkb_state_notify_event_t *state)
{
	xkb_state_update_mask(c->xkb.state,
			      get_xkb_mod_mask(c, state->baseMods),
			      get_xkb_mod_mask(c, state->latchedMods),
			      get_xkb_mod_mask(c, state->lockedMods),
			      0,
			      0,
			      state->group);

	wlb_keyboard_modifiers(c->keyboard,
			       get_xkb_mod_mask(c, state->baseMods),
			       get_xkb_mod_mask(c, state->latchedMods),
			       get_xkb_mod_mask(c, state->lockedMods),
			       0);
}
#endif

/**
 * This is monumentally unpleasant.  If we don't have XCB-XKB bindings,
 * the best we can do (given that XCB also lacks XI2 support), is to take
 * the state from the core key events.  Unfortunately that only gives us
 * the effective (i.e. union of depressed/latched/locked) state, and we
 * need the granularity.
 *
 * So we still update the state with every key event we see, but also use
 * the state field from X11 events as a mask so we don't get any stuck
 * modifiers.
 */
static void
update_xkb_state_from_core(struct x11_compositor *c, uint16_t x11_mask)
{
	uint32_t mask = get_xkb_mod_mask(c, x11_mask);

	xkb_state_update_mask(c->xkb.state,
			      c->xkb.mods_depressed & mask,
			      c->xkb.mods_latched & mask,
			      c->xkb.mods_locked & mask,
			      0,
			      0,
			      (x11_mask >> 13) & 3);

	c->xkb.mods_depressed =
		xkb_state_serialize_mods(c->xkb.state, XKB_STATE_DEPRESSED);
	c->xkb.mods_latched =
		xkb_state_serialize_mods(c->xkb.state, XKB_STATE_LATCHED);
	c->xkb.mods_locked =
		xkb_state_serialize_mods(c->xkb.state, XKB_STATE_LOCKED);

	wlb_keyboard_modifiers(c->keyboard,
			       c->xkb.mods_depressed,
			       c->xkb.mods_latched,
			       c->xkb.mods_locked,
			       0);
}

static void
x11_compositor_deliver_button_event(struct x11_compositor *c,
				    xcb_generic_event_t *event, int state)
{
	xcb_button_press_event_t *button_event =
		(xcb_button_press_event_t *) event;
	uint32_t button;
	struct x11_output *output;

	output = x11_compositor_find_output(c, button_event->event);

	if (state)
		xcb_grab_pointer(c->conn, 0, output->window,
				 XCB_EVENT_MASK_BUTTON_PRESS |
				 XCB_EVENT_MASK_BUTTON_RELEASE |
				 XCB_EVENT_MASK_POINTER_MOTION |
				 XCB_EVENT_MASK_ENTER_WINDOW |
				 XCB_EVENT_MASK_LEAVE_WINDOW,
				 XCB_GRAB_MODE_ASYNC,
				 XCB_GRAB_MODE_ASYNC,
				 output->window, XCB_CURSOR_NONE,
				 button_event->time);
	else
		xcb_ungrab_pointer(c->conn, button_event->time);

	if (!c->has_xkb)
		update_xkb_state_from_core(c, button_event->state);

	switch (button_event->detail) {
	default:
		button = button_event->detail + BTN_LEFT - 1;
		break;
	case 2:
		button = BTN_MIDDLE;
		break;
	case 3:
		button = BTN_RIGHT;
		break;
	case 4:
		/* Axis are measured in pixels, but the xcb events are discrete
		 * steps. Therefore move the axis by some pixels every step. */
		if (state)
			wlb_pointer_axis(c->pointer,
					 x11_compositor_get_time(),
					 WL_POINTER_AXIS_VERTICAL_SCROLL,
					 -DEFAULT_AXIS_STEP_DISTANCE);
		return;
	case 5:
		if (state)
			wlb_pointer_axis(c->pointer,
					 x11_compositor_get_time(),
					 WL_POINTER_AXIS_VERTICAL_SCROLL,
					 DEFAULT_AXIS_STEP_DISTANCE);
		return;
	case 6:
		if (state)
			wlb_pointer_axis(c->pointer,
					 x11_compositor_get_time(),
					 WL_POINTER_AXIS_HORIZONTAL_SCROLL,
					 -DEFAULT_AXIS_STEP_DISTANCE);
		return;
	case 7:
		if (state)
			wlb_pointer_axis(c->pointer,
					 x11_compositor_get_time(),
					 WL_POINTER_AXIS_HORIZONTAL_SCROLL,
					 DEFAULT_AXIS_STEP_DISTANCE);
		return;
	}

	wlb_pointer_button(c->pointer, x11_compositor_get_time(), button,
			   state ? WL_POINTER_BUTTON_STATE_PRESSED :
				   WL_POINTER_BUTTON_STATE_RELEASED);
}

static void
x11_compositor_deliver_motion_event(struct x11_compositor *c,
					xcb_generic_event_t *event)
{
	struct x11_output *output;
	wl_fixed_t x, y;
	xcb_motion_notify_event_t *motion_notify =
			(xcb_motion_notify_event_t *) event;

	if (!c->has_xkb)
		update_xkb_state_from_core(c, motion_notify->state);
	output = x11_compositor_find_output(c, motion_notify->event);

	wlb_pointer_move_on_output(c->pointer, x11_compositor_get_time(),
				   output->output,
				   wl_fixed_from_int(motion_notify->event_x),
				   wl_fixed_from_int(motion_notify->event_y));
}

static int
x11_compositor_next_event(struct x11_compositor *c,
			  xcb_generic_event_t **event, uint32_t mask)
{
	if (mask & WL_EVENT_READABLE) {
		*event = xcb_poll_for_event(c->conn);
	} else {
#ifdef HAVE_XCB_POLL_FOR_QUEUED_EVENT
		*event = xcb_poll_for_queued_event(c->conn);
#else
		*event = xcb_poll_for_event(c->conn);
#endif
	}

	return *event != NULL;
}

static int
x11_compositor_handle_event(int fd, uint32_t mask, void *data)
{
	struct x11_compositor *c = data;
	struct x11_output *output;
	xcb_generic_event_t *event, *prev;
	xcb_client_message_event_t *client_message;
	xcb_enter_notify_event_t *enter_notify;
	xcb_key_press_event_t *key_press, *key_release;
	xcb_keymap_notify_event_t *keymap_notify;
	xcb_focus_in_event_t *focus_in;
	xcb_expose_event_t *expose;
	xcb_atom_t atom;
	uint32_t *k;
	uint32_t i, set;
	uint8_t response_type;
	int count;

	prev = NULL;
	count = 0;
	while (x11_compositor_next_event(c, &event, mask)) {
		response_type = event->response_type & ~0x80;

		switch (prev ? prev->response_type & ~0x80 : 0x80) {
		case XCB_KEY_RELEASE:
			/* Suppress key repeat events; this is only used if we
			 * don't have XCB XKB support. */
			key_release = (xcb_key_press_event_t *) prev;
			key_press = (xcb_key_press_event_t *) event;
			if (response_type == XCB_KEY_PRESS &&
			    key_release->time == key_press->time &&
			    key_release->detail == key_press->detail) {
				/* Don't deliver the held key release
				 * event or the new key press event. */
				free(event);
				free(prev);
				prev = NULL;
				continue;
			} else {
				/* Deliver the held key release now
				 * and fall through and handle the new
				 * event below. */
				update_xkb_state_from_core(c, key_release->state);
				wlb_keyboard_key(c->keyboard,
						 x11_compositor_get_time(),
						 key_release->detail - 8,
						 WL_KEYBOARD_KEY_STATE_RELEASED);
				free(prev);
				prev = NULL;
				break;
			}

		case XCB_FOCUS_IN:
			assert(response_type == XCB_KEYMAP_NOTIFY);
			keymap_notify = (xcb_keymap_notify_event_t *) event;
			c->keys.size = 0;
			for (i = 0; i < ARRAY_LENGTH(keymap_notify->keys) * 8; i++) {
				set = keymap_notify->keys[i >> 3] &
					(1 << (i & 7));
				if (set) {
					k = wl_array_add(&c->keys, sizeof *k);
					*k = i;
				}
			}

			/* Unfortunately the state only comes with the enter
			 * event, rather than with the focus event.  I'm not
			 * sure of the exact semantics around it and whether
			 * we can ensure that we get both? */
			wlb_keyboard_enter(c->keyboard, &c->keys);

			free(prev);
			prev = NULL;
			break;

		default:
			/* No previous event held */
			break;
		}

		switch (response_type) {
		case XCB_KEY_PRESS:
			key_press = (xcb_key_press_event_t *) event;
			if (!c->has_xkb)
				update_xkb_state_from_core(c, key_press->state);
			wlb_keyboard_key(c->keyboard,
					 x11_compositor_get_time(),
					 key_press->detail - 8,
					 WL_KEYBOARD_KEY_STATE_PRESSED);
			break;
		case XCB_KEY_RELEASE:
			/* If we don't have XKB, we need to use the lame
			 * autorepeat detection above. */
			if (!c->has_xkb) {
				prev = event;
				break;
			}
			key_release = (xcb_key_press_event_t *) event;
			wlb_keyboard_key(c->keyboard,
					 x11_compositor_get_time(),
					 key_release->detail - 8,
					 WL_KEYBOARD_KEY_STATE_RELEASED);
			break;
		case XCB_BUTTON_PRESS:
			x11_compositor_deliver_button_event(c, event, 1);
			break;
		case XCB_BUTTON_RELEASE:
			x11_compositor_deliver_button_event(c, event, 0);
			break;
		case XCB_MOTION_NOTIFY:
			x11_compositor_deliver_motion_event(c, event);
			break;

#if 0
		case XCB_EXPOSE:
			expose = (xcb_expose_event_t *) event;
			output = x11_compositor_find_output(c, expose->window);
			weston_output_schedule_repaint(&output->base);
			break;

		case XCB_ENTER_NOTIFY:
			x11_compositor_deliver_enter_event(c, event);
			break;

		case XCB_LEAVE_NOTIFY:
			enter_notify = (xcb_enter_notify_event_t *) event;
			if (enter_notify->state >= Button1Mask)
				break;
			if (!c->has_xkb)
				update_xkb_state_from_core(c, enter_notify->state);
			notify_pointer_focus(&c->core_seat, NULL, 0, 0);
			break;
#endif

		case XCB_CLIENT_MESSAGE:
			client_message = (xcb_client_message_event_t *) event;
			atom = client_message->data.data32[0];
			if (atom == c->atom.wm_delete_window)
				wl_display_terminate(c->display);
			break;

		case XCB_FOCUS_IN:
			focus_in = (xcb_focus_in_event_t *) event;
			if (focus_in->mode == XCB_NOTIFY_MODE_WHILE_GRABBED)
				break;

			prev = event;
			break;

		case XCB_FOCUS_OUT:
			focus_in = (xcb_focus_in_event_t *) event;
			if (focus_in->mode == XCB_NOTIFY_MODE_WHILE_GRABBED ||
			    focus_in->mode == XCB_NOTIFY_MODE_UNGRAB)
				break;
			wlb_keyboard_leave(c->keyboard);
			break;

		default:
			break;
		}

#ifdef HAVE_XCB_XKB
		if (c->has_xkb) {
			if (response_type == c->xkb_event_base) {
				xcb_xkb_state_notify_event_t *state =
					(xcb_xkb_state_notify_event_t *) event;
				if (state->xkbType == XCB_XKB_STATE_NOTIFY)
					update_xkb_state(c, state);
			} else if (response_type == XCB_PROPERTY_NOTIFY) {
				xcb_property_notify_event_t *prop_notify =
					(xcb_property_notify_event_t *) event;
				if (prop_notify->window == c->screen->root &&
				    prop_notify->atom == c->atom.xkb_names &&
				    prop_notify->state == XCB_PROPERTY_NEW_VALUE)
					update_xkb_keymap(c);
			}
		}
#endif

		count++;
		if (prev != event)
			free (event);
	}

	switch (prev ? prev->response_type & ~0x80 : 0x80) {
	case XCB_KEY_RELEASE:
		key_release = (xcb_key_press_event_t *) prev;
		update_xkb_state_from_core(c, key_release->state);
		/* XXX: Should be STATE_UPDATE_AUTOMATIC */
		wlb_keyboard_key(c->keyboard, x11_compositor_get_time(),
				 key_release->detail - 8,
				 WL_KEYBOARD_KEY_STATE_RELEASED);
		free(prev);
		prev = NULL;
		break;
	default:
		break;
	}

	return count;
}

struct x11_compositor *
x11_compositor_create(struct wl_display *display)
{
	struct x11_compositor *c;
	xcb_screen_iterator_t siter;
	struct wl_event_loop *loop;

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

	if (x11_input_create(c) < 0)
		goto err_xdisplay;

	wl_list_init(&c->output_list);

	loop = wl_display_get_event_loop(c->display);
	c->xcb_source =
		wl_event_loop_add_fd(loop, xcb_get_file_descriptor(c->conn),
				     WL_EVENT_READABLE,
				     x11_compositor_handle_event, c);
	wl_event_source_check(c->xcb_source);

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
	uint32_t msec;
	struct timeval tv;
	
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

	wlb_output_repaint_complete(output->output, x11_compositor_get_time());

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

	values[0] |=
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_ENTER_WINDOW |
		XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_KEYMAP_STATE |
		XCB_EVENT_MASK_FOCUS_CHANGE;

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

	wl_list_insert(&c->output_list, &output->compositor_link);
	
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
