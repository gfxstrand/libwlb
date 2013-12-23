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
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <assert.h>
#include <errno.h>

struct wlb_buffer_type_item {
	struct wl_list link;

	struct wlb_buffer_type *type;
	void *type_data;
	size_t type_size;
};

static void
compositor_create_surface(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id)
{
	struct wlb_compositor *compositor = wl_resource_get_user_data(resource);
	struct wlb_surface *surface;

	surface = wlb_surface_create(compositor, client, id);
	if (!surface)
		wl_client_post_no_memory(client);
}

static void
region_add(struct wl_client *client, struct wl_resource *resource,
	   int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);

	pixman_region32_union_rect(&region->region, &region->region,
				   x, y, width, height);
}

static void
region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);
	pixman_region32_t rect;

	pixman_region32_init_rect(&rect, x, y, width, height);
	pixman_region32_subtract(&region->region, &region->region, &rect);
	pixman_region32_fini(&rect);
}

static void
region_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_region_interface region_interface = {
	region_destroy,
	region_add,
	region_subtract
};

static void
destroy_region(struct wl_resource *resource)
{
	struct wlb_region *region = wl_resource_get_user_data(resource);

	pixman_region32_fini(&region->region);
	free(region);
}

static void
compositor_create_region(struct wl_client *client,
			 struct wl_resource *resource, uint32_t id)
{
	struct wlb_region *region;

	region = zalloc(sizeof *region);
	if (!region) {
		wl_client_post_no_memory(client);
		return;
	}

	region->resource =
		wl_resource_create(client, &wl_region_interface, 1, id);
	if (!region->resource) {
		wl_client_post_no_memory(client);
		free(region);
		return;
	}
	wl_resource_set_implementation(region->resource, &region_interface,
				       region, destroy_region);

	pixman_region32_init(&region->region);
}

static const struct wl_compositor_interface compositor_interface = {
	compositor_create_surface,
	compositor_create_region
};

static void
compositor_bind(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct wlb_compositor *comp = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_compositor_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &compositor_interface,
				       comp, NULL);
}

static void
shell_present_surface(struct wl_client *client, struct wl_resource *resource,
		      struct wl_resource *surface_res, uint32_t method,
		      uint32_t framerate, struct wl_resource *output_res)
{
	struct wlb_compositor *comp = wl_resource_get_user_data(resource);
	struct wlb_surface *surface = NULL;
	struct wlb_output *output = NULL;

	if (surface_res)
		surface = wl_resource_get_user_data(surface_res);
	if (output_res)
		output = wl_resource_get_user_data(output_res);
	
	if (output) {
		wlb_output_present_surface(output, surface, method, framerate);
	} else {
		wl_list_for_each(output, &comp->output_list, compositor_link)
			wlb_output_present_surface(output, surface,
						   method, framerate);
	}
}

static const struct wl_fullscreen_shell_interface fullscreen_shell_interface = {
	shell_present_surface
};

static void
fullscreen_shell_bind(struct wl_client *client,
		      void *data, uint32_t version, uint32_t id)
{
	struct wlb_compositor *comp = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_fullscreen_shell_interface,
				      1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource, &fullscreen_shell_interface,
				       comp, NULL);
}

static int
shm_buffer_is_type(void *data, struct wl_resource *buffer)
{
	return wl_shm_buffer_get(buffer) ? 1 : 0;
}

static void
shm_buffer_get_size(void *data, struct wl_resource *buffer,
		    int32_t *width, int32_t *height)
{
	struct wl_shm_buffer *shm_buffer;

	shm_buffer = wl_shm_buffer_get(buffer);
	assert(shm_buffer);

	*width = wl_shm_buffer_get_width(shm_buffer);
	*height = wl_shm_buffer_get_height(shm_buffer);
}

static void *
shm_buffer_mmap(void *data, struct wl_resource *buffer,
		uint32_t *stride, uint32_t *format)
{
	struct wl_shm_buffer *shm_buffer;

	shm_buffer = wl_shm_buffer_get(buffer);
	assert(shm_buffer);

	*stride = wl_shm_buffer_get_stride(shm_buffer);
	*format = wl_shm_buffer_get_format(shm_buffer);

	return wl_shm_buffer_get_data(shm_buffer);
}

static void
shm_buffer_munmap(void *data, struct wl_resource *buffer, void *mapped)
{
	/* XXX: Protect memory */
}

struct wlb_buffer_type shm_buffer_type = {
	shm_buffer_is_type,
	shm_buffer_get_size,
	shm_buffer_mmap,
	shm_buffer_munmap
};

WL_EXPORT struct wlb_compositor *
wlb_compositor_create(struct wl_display *display)
{
	struct wlb_compositor *comp;

	comp = zalloc(sizeof *comp);
	if (!comp)
		return NULL;
	
	comp->display = display;

	wl_list_init(&comp->buffer_type_list);

	wl_list_init(&comp->output_list);
	wl_list_init(&comp->seat_list);
	
	if (!wl_global_create(display, &wl_compositor_interface, 1,
			      comp, compositor_bind))
		goto err_alloc;

	if (!wl_global_create(display, &wl_fullscreen_shell_interface, 1,
			      comp, fullscreen_shell_bind))
		goto err_alloc;
	
	wlb_compositor_add_buffer_type(comp, &shm_buffer_type, NULL);

	return comp;

err_alloc:
	free(comp);
	return NULL;
}

WL_EXPORT void
wlb_compositor_destroy(struct wlb_compositor *comp)
{
	struct wlb_output *output, *onext;
	struct wlb_seat *seat, *snext;
	struct wlb_buffer_type_item *item, *inext;

	wl_list_for_each_safe(output, onext, &comp->output_list, compositor_link)
		wlb_output_destroy(output);

	wl_list_for_each_safe(seat, snext, &comp->seat_list, compositor_link)
		wlb_seat_destroy(seat);

	wl_list_for_each_safe(item, inext, &comp->buffer_type_list, link) {
		wl_list_remove(&item->link);
		free(item);
	}
	
	wl_display_destroy(comp->display);

	free(comp);
}

WL_EXPORT struct wl_display *
wlb_compositor_get_display(struct wlb_compositor *comp)
{
	return comp->display;
}

WL_EXPORT int
wlb_compositor_add_buffer_type_with_size(struct wlb_compositor *comp,
					 struct wlb_buffer_type *type,
					 void *data, size_t size)
{
	struct wlb_buffer_type_item *item;

	if (!type || !size) {
		wlb_error("Tried to register null buffer type");
		errno = EINVAL;
		return -1;
	}

	item = zalloc(sizeof *item);
	if (!item)
		return -1;

	item->type = type;
	item->type_data = data;
	item->type_size = size;

	wl_list_insert(&comp->buffer_type_list, &item->link);

	return 0;
}

WL_EXPORT struct wlb_buffer_type *
wlb_compositor_get_buffer_type(struct wlb_compositor *comp,
			       struct wl_resource *buffer,
			       void **data, size_t *size)
{
	struct wlb_buffer_type_item *item;

	wl_list_for_each(item, &comp->buffer_type_list, link) {
		if (item->type->is_type(item->type_data, buffer)) {
			*data = item->type_data;
			*size = item->type_size;
			return item->type;
		}
	}

	return NULL;
}

WL_EXPORT struct wl_client *
wlb_compositor_launch_client(struct wlb_compositor *compositor,
			     const char *exec_path, char * const argv[])
{
	struct wl_client *client;
	int sockets[2];
	char fd_str[12];
	pid_t pid;

	wlb_debug("Starting client: %s\n", exec_path);

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) {
		wlb_error("socketpair() failed: %s\n", strerror(errno));
		return NULL;
	}

	pid = fork();
	if (pid == -1) {
		close(sockets[0]);
		close(sockets[1]);
		wlb_error("fork() failed: %s\n", strerror(errno));
		return NULL;
	} else if (pid == 0) { /* Child */
		close(sockets[0]);

		snprintf(fd_str, sizeof(fd_str), "%d", sockets[1]);
		setenv("WAYLAND_SOCKET", fd_str, 1);

		if (argv) {
			execv(exec_path, argv);
		} else {
			execl(exec_path, exec_path, NULL);
		}

		wlb_error("execv() failed: %s\n", strerror(errno));
		exit(-1);
	} else { /* Parent */
		close(sockets[1]);
		client = wl_client_create(compositor->display, sockets[0]);
		if (!client) {
			wlb_error("Failed to create client\n");
			close(sockets[0]);
			return NULL;
		}
		return client;
	}
} 
