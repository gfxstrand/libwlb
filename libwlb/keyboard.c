/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2013 Intel Corporation
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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static void
keyboard_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_keyboard *keyboard =
		wl_container_of(listener, keyboard, surface_destroy_listener);

	wlb_keyboard_set_focus(keyboard, NULL);
}

WL_EXPORT struct wlb_keyboard *
wlb_keyboard_create(struct wlb_seat *seat)
{
	struct wlb_keyboard *keyboard;

	if (seat->keyboard)
		return NULL;

	keyboard = zalloc(sizeof *keyboard);
	if (!keyboard)
		return NULL;
	
	keyboard->seat = seat;

	wl_list_init(&keyboard->resource_list);
	wl_array_init(&keyboard->keys);
	keyboard->keymap.fd = -1;
	
	keyboard->surface_destroy_listener.notify = keyboard_surface_destroyed;

	seat->keyboard = keyboard;

	return keyboard;
}

WL_EXPORT void
wlb_keyboard_destroy(struct wlb_keyboard *keyboard)
{
	struct wl_resource *resource, *rnext;

	wl_resource_for_each_safe(resource, rnext, &keyboard->resource_list)
		wl_resource_destroy(resource);
	
	keyboard->seat->keyboard = NULL;
	
	if (keyboard->keymap.data) {
		munmap(keyboard->keymap.data, keyboard->keymap.size);
		close(keyboard->keymap.fd);
	}

	free(keyboard);
}

static void
keyboard_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

struct wl_keyboard_interface keyboard_interface = {
	keyboard_release
};

static void
unlink_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

void
wlb_keyboard_create_resource(struct wlb_keyboard *keyboard,
			     struct wl_client *client, uint32_t id)
{
	struct wl_resource *resource;
	int null_fd;

	resource = wl_resource_create(client, &wl_keyboard_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &keyboard_interface,
				       NULL, unlink_resource);

	wl_list_insert(&keyboard->resource_list, wl_resource_get_link(resource));

	if (keyboard->keymap.data) {
		wl_keyboard_send_keymap(resource, keyboard->keymap.format,
					keyboard->keymap.fd,
					keyboard->keymap.size);
	} else {
		null_fd = open("/dev/null", O_RDONLY);
		wl_keyboard_send_keymap(resource,
					WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP,
					null_fd, 0);
		close(null_fd);
	}
}

void
wlb_keyboard_set_focus(struct wlb_keyboard *keyboard,
		       struct wlb_surface *focus)
{
	struct wl_resource *resource;
	uint32_t serial;

	if (keyboard->focus == focus)
		return;

	serial = wl_display_next_serial(keyboard->seat->compositor->display);
	
	if (keyboard->focus) {
		wl_resource_for_each(resource, &keyboard->resource_list)
			wl_keyboard_send_leave(resource, serial,
					       keyboard->focus->resource);

		wl_list_remove(&keyboard->surface_destroy_listener.link);
	}

	keyboard->focus = focus;
	
	if (keyboard->focus) {
		wl_resource_add_destroy_listener(focus->resource,
						 &keyboard->surface_destroy_listener);

		wl_resource_for_each(resource, &keyboard->resource_list)
			wl_keyboard_send_enter(resource, serial,
					       keyboard->focus->resource,
					       &keyboard->keys);
	}
}

WL_EXPORT int
wlb_keyboard_set_keymap(struct wlb_keyboard *keyboard, const void *data,
			size_t size, enum wl_keyboard_keymap_format format)
{
	/* We don't want to handle the keymap switch case for now */
	if (keyboard->keymap.data)
		return -1;

	if (!data || !size)
		return 0;

	keyboard->keymap.fd = wlb_util_create_tmpfile(size);
	if (keyboard->keymap.fd < 0)
		return -1;
	keyboard->keymap.data = mmap(NULL, size, PROT_READ | PROT_WRITE,
				     MAP_SHARED, keyboard->keymap.fd, 0);
	if (keyboard->keymap.data == MAP_FAILED) {
		close(keyboard->keymap.fd);
		keyboard->keymap.fd = -1;
		return -1;
	}
	keyboard->keymap.size = size;
	keyboard->keymap.format = format;
	memcpy(keyboard->keymap.data, data, size);

	return 0;
}

static void
keyboard_ensure_focus(struct wlb_keyboard *keyboard)
{
	if (keyboard->seat->pointer &&
	    keyboard->seat->pointer->focus &&
	    keyboard->seat->pointer->focus_surface != keyboard->focus)
		wlb_keyboard_set_focus(keyboard,
				       keyboard->seat->pointer->focus_surface);
}

WL_EXPORT void
wlb_keyboard_key(struct wlb_keyboard *keyboard, uint32_t time,
		 uint32_t key, enum wl_keyboard_key_state state)
{
	struct wl_resource *resource;
	uint32_t serial, *k, *end;

	keyboard_ensure_focus(keyboard);

	end = keyboard->keys.data + keyboard->keys.size;
	for (k = keyboard->keys.data; k < end; k++) {
		if (*k == key) {
			/* Ignore server-generated repeats. */
			if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
				return;
			*k = *--end;
		}
	}
	keyboard->keys.size = (void *) end - keyboard->keys.data;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		k = wl_array_add(&keyboard->keys, sizeof *k);
		*k = key;
	}

	if (!keyboard->focus || wl_list_empty(&keyboard->resource_list))
		return;
	
	serial = wl_display_next_serial(keyboard->seat->compositor->display);
	wl_resource_for_each(resource, &keyboard->resource_list)
		wl_keyboard_send_key(resource, serial, time, key, state);
}

WL_EXPORT void
wlb_keyboard_modifiers(struct wlb_keyboard *keyboard, uint32_t mods_depressed,
		       uint32_t mods_latched, uint32_t mods_locked,
		       uint32_t group)
{
	struct wl_resource *resource;
	uint32_t serial;

	keyboard_ensure_focus(keyboard);

	if (!keyboard->focus || wl_list_empty(&keyboard->resource_list))
		return;
	
	serial = wl_display_next_serial(keyboard->seat->compositor->display);
	wl_resource_for_each(resource, &keyboard->resource_list)
		wl_keyboard_send_modifiers(resource, serial, mods_depressed,
					   mods_latched, mods_locked, group);
}

WL_EXPORT void
wlb_keyboard_enter(struct wlb_keyboard *keyboard, const struct wl_array *keys)
{
	/* TODO */
}

WL_EXPORT void
wlb_keyboard_leave(struct wlb_keyboard *keyboard)
{
	/* TODO */
}
