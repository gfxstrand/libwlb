/*
 * Copyright © 2014 Jason Ekstrand
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
#include <assert.h>

enum presentation_flags {
	PRESENTATION_ACTIVE = 0x1,
	PRESENTATION_FOR_MODE = 0x2,
};

struct wlb_presentation {
	struct wlb_fullscreen_shell *fshell;
	struct wl_list link;
	
	struct wlb_output *output;
	struct wl_listener output_destroyed;

	struct wlb_surface *surface;
	struct wl_listener surface_destroyed;
	struct wl_listener surface_committed;

	uint32_t flags;
	int32_t framerate;
	enum wl_fullscreen_shell_present_method method;
	struct wl_resource *mode_feedback;
};

struct wlb_fullscreen_shell {
	struct wlb_compositor *compositor;
	struct wl_global *global;

	struct wl_list presentation_list;
};

static void
wlb_presentation_destroy(struct wlb_presentation *pres);
static void
wlb_presentation_cofigure(struct wlb_presentation *pres);
static void
wlb_presentation_cofigure_for_mode(struct wlb_presentation *pres);

static void
presentation_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_presentation *pres;

	pres = wl_container_of(listener, pres, surface_destroyed);
	wlb_presentation_destroy(pres);
}

static void
presentation_surface_committed(struct wl_listener *listener, void *data)
{
	struct wlb_presentation *pres;

	pres = wl_container_of(listener, pres, surface_committed);

	/* Note that there is a potential list corruption here that we very
	 * carefully avoid.  If multiple presentations are active on the same
	 * surface, the commit signal may cause one of them to be actiated and
	 * another removed.  However, we ensure that the only one removed is
	 * the currently active one whose listener is earlier in the list.
	 * Therefore, its signal has already been notified and and we don't
	 * have any issues.
	 */

	if (pres->flags & PRESENTATION_FOR_MODE)
		wlb_presentation_cofigure_for_mode(pres);
	else
		wlb_presentation_cofigure(pres);
}

static void
presentation_output_destroyed(struct wl_listener *listener, void *data)
{
	struct wlb_presentation *pres;

	pres = wl_container_of(listener, pres, output_destroyed);
	wlb_presentation_destroy(pres);
}

static struct wlb_presentation *
wlb_presentation_create(struct wlb_fullscreen_shell *fshell,
			struct wlb_surface *surface,
			struct wlb_output *output)
{
	struct wlb_presentation *pres, *np;

	/* We only want one pending presentation on any given output at any
	 * given time.  Destroy the rest before we create this one. */
	wl_list_for_each_safe(pres, np, &fshell->presentation_list, link)
		if (pres->output == output &&
		    !(pres->flags & PRESENTATION_ACTIVE))
			wlb_presentation_destroy(pres);

	pres = zalloc(sizeof *pres);
	if (!pres)
		return NULL;

	pres->fshell = fshell;

	pres->surface = surface;
	pres->surface_destroyed.notify = presentation_surface_destroyed;
	wl_signal_add(&pres->surface->destroy_signal, &pres->surface_destroyed);
	pres->surface_committed.notify = presentation_surface_committed;
	wl_signal_add(&pres->surface->commit_signal, &pres->surface_committed);

	pres->output = output;
	pres->output_destroyed.notify = presentation_output_destroyed;
	wl_signal_add(&pres->output->destroy_signal, &pres->output_destroyed);

	wl_list_insert(&fshell->presentation_list, &pres->link);

	return pres;
}

static void
wlb_presentation_destroy(struct wlb_presentation *pres)
{
	wl_list_remove(&pres->surface_destroyed.link);
	wl_list_remove(&pres->surface_committed.link);

	if (pres->mode_feedback) {
		wl_fullscreen_shell_mode_feedback_send_present_canceled(
			pres->mode_feedback);
		wl_resource_destroy(pres->mode_feedback);
	}

	wl_list_remove(&pres->output_destroyed.link);

	wl_list_remove(&pres->link);

	free(pres);
}

/* Promotes this presentation to the active one, removing all others with the
 * same output. */
static void
wlb_presentation_promote(struct wlb_presentation *pres)
{
	struct wlb_presentation *other, *pn;

	/* Get rid of all of the others associated with the same output */
	wl_list_for_each_safe(other, pn, &pres->fshell->presentation_list, link) 
		if (other->output == pres->output && other != pres)
			wlb_presentation_destroy(other);
	
	pres->flags |= PRESENTATION_ACTIVE;
}

static void
wlb_presentation_cofigure(struct wlb_presentation *pres)
{
	int32_t ow, oh, sw, sh;
	struct wlb_rectangle pos;
	int ret;

	/* This one is unconditional; no fallback */
	if (! (pres->flags & PRESENTATION_ACTIVE))
		wlb_presentation_promote(pres);

	/* First we use the user-provided positioning if we can */
	if (WLB_HAS_FUNC(pres->output, place_surface)) {
		ret = WLB_CALL_FUNC(pres->output, place_surface,
				    pres->surface, pres->method, &pos);
		if (ret > 0) {
			wlb_output_set_surface(pres->output,
					       pres->surface, &pos);
			return;
		}
	}

	/* No user-provided positioning; falling back to the default. */

	sw = pres->surface->width;
	sh = pres->surface->height;

	ow = pres->output->width;
	oh = pres->output->height;

	switch(pres->method) {
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_DEFAULT:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_CENTER:
		pos.x = (ow - sw) / 2;
		pos.y = (oh - sh) / 2;
		pos.width = sw;
		pos.height = sh;

		break;
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_ZOOM:
		if (ow * sh <= oh * sw) {
			pos.width = ow;
			pos.height = (sh * (int64_t)ow) / sw;
			pos.x = 0;
			pos.y = (int32_t)(oh - pos.height) / 2;
		} else {
			pos.width = (sw * (int64_t)oh) / sh;
			pos.height = oh;
			pos.x = (int32_t)(ow - pos.width) / 2;
			pos.y = 0;
		}

		break;
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_ZOOM_CROP:
		if (ow * sh >= oh * sw) {
			pos.width = ow;
			pos.height = (sh * (int64_t)ow) / sw;
			pos.x = 0;
			pos.y = (int32_t)(oh - pos.height) / 2;
		} else {
			pos.width = (sw * (int64_t)oh) / sh;
			pos.height = oh;
			pos.x = (int32_t)(ow - pos.width) / 2;
			pos.y = 0;
		}

		break;
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_STRETCH:
		pos.x = 0;
		pos.y = 0;
		pos.width = ow;
		pos.height = oh;

		break;
	}

	wlb_output_set_surface(pres->output, pres->surface, &pos);
}

static void
wlb_presentation_cofigure_for_mode(struct wlb_presentation *pres)
{
	struct wlb_rectangle pos;
	int32_t sw, sh, ow, oh;
	int success = 0;

	sw = pres->surface->width;
	sh = pres->surface->height;

	ow = pres->output->width;
	oh = pres->output->height;

	/* Automatically succeed if a mode-switch is not needed */
	if (sw == ow && sh == oh)
		success = 1;

	if (success <= 0 && WLB_HAS_FUNC(pres->output, switch_mode))
		success = WLB_CALL_FUNC(pres->output, switch_mode,
					pres->surface->width, pres->surface->height,
					pres->framerate);
	if (success > 0) {
		if (! (pres->flags & PRESENTATION_ACTIVE))
			wlb_presentation_promote(pres);

		if (pres->mode_feedback) {
			wl_fullscreen_shell_mode_feedback_send_mode_successful(
				pres->mode_feedback);
			wl_resource_destroy(pres->mode_feedback);
			pres->mode_feedback = NULL;
		}
	} else {
		if (pres->mode_feedback) {
			wl_fullscreen_shell_mode_feedback_send_mode_failed(
				pres->mode_feedback);
			wl_resource_destroy(pres->mode_feedback);
			pres->mode_feedback = NULL;
		}
		
		if (! (pres->flags & PRESENTATION_ACTIVE)) {
			wlb_presentation_destroy(pres);
			return;
		}
	}

	pos.x = (ow - sw) / 2;
	pos.y = (oh - sh) / 2;
	pos.width = sw;
	pos.height = sh;

	wlb_output_set_surface(pres->output, pres->surface, &pos);
}

static void
wlb_fullscreen_shell_clear_output(struct wlb_fullscreen_shell *fshell,
				  struct wlb_output *output)
{
	struct wlb_presentation *pres, *n;

	wlb_output_set_surface(output, NULL, NULL);

	wl_list_for_each_safe(pres, n, &fshell->presentation_list, link)
		if (pres->output == output)
			wlb_presentation_destroy(pres);
}

static void
fullscreen_shell_release(struct wl_client *client,
			 struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
present_surface_helper(struct wlb_fullscreen_shell *fshell,
		       struct wlb_surface *surface, struct wlb_output *output,
		       enum wl_fullscreen_shell_present_method method)
{
	struct wlb_presentation *pres;

	if (!surface) {
		wlb_fullscreen_shell_clear_output(fshell, output);
		return;
	}

	pres = wlb_presentation_create(fshell, surface, output);
	if (!pres) {
		wlb_error("Out of Memory\n");
		return;
	}

	pres->method = method;
	pres->flags = 0;
}

static void
fullscreen_shell_present_surface(struct wl_client *client,
				 struct wl_resource *resource,
				 struct wl_resource *surface_res,
				 uint32_t method,
				 struct wl_resource *output_res)
{
	struct wlb_fullscreen_shell *fshell = wl_resource_get_user_data(resource);
	struct wlb_compositor *comp = fshell->compositor;
	struct wlb_output *output;
	struct wlb_surface *surface;

	surface = surface_res ? wl_resource_get_user_data(surface_res) : NULL;

	switch (method) {
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_DEFAULT:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_CENTER:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_ZOOM:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_ZOOM_CROP:
	case WL_FULLSCREEN_SHELL_PRESENT_METHOD_STRETCH:
		break;
	default:
		wl_resource_post_error(resource,
				       WL_FULLSCREEN_SHELL_ERROR_INVALID_METHOD,
				       "Invalid present_method argument");
		return;
	}

	if (output_res) {
		output = wl_resource_get_user_data(output_res);
		present_surface_helper(fshell, surface, output, method);
	} else {
		/* output == NULL -> all outputs */
		wl_list_for_each(output, &comp->output_list, compositor_link)
			present_surface_helper(fshell, surface, output, method);
	}
}

static void
fullscreen_shell_present_surface_for_mode(struct wl_client *client,
					  struct wl_resource *resource,
					  struct wl_resource *surface_res,
					  struct wl_resource *output_res,
					  int32_t framerate,
					  uint32_t feedback_id)
{
	struct wlb_fullscreen_shell *fshell = wl_resource_get_user_data(resource);
	struct wlb_presentation *pres;
	struct wlb_surface *surface;
	struct wlb_output *output;

	assert(surface_res != NULL);
	assert(output_res != NULL);

	surface = wl_resource_get_user_data(surface_res);
	output = wl_resource_get_user_data(output_res);

	pres = wlb_presentation_create(fshell, surface, output);
	if (!pres) {
		wlb_error("Out of Memory\n");
		return;
	}

	pres->flags = PRESENTATION_FOR_MODE;
	pres->framerate = framerate;

	pres->mode_feedback = 
		wl_resource_create(client,
				   &wl_fullscreen_shell_mode_feedback_interface,
				   1, feedback_id);
	if (!pres->mode_feedback) {
		wlb_error("Out of Memory\n");
		wlb_presentation_destroy(pres);
		return;
	}
}

struct wl_fullscreen_shell_interface fullscreen_shell_implementation = {
	fullscreen_shell_release,
	fullscreen_shell_present_surface,
	fullscreen_shell_present_surface_for_mode,
};

static void
fullscreen_shell_bind(struct wl_client *client,
		      void *data, uint32_t version, uint32_t id)
{
	struct wlb_fullscreen_shell *fshell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &wl_fullscreen_shell_interface,
				      1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}

	wl_resource_set_implementation(resource,
				       &fullscreen_shell_implementation,
				       fshell, NULL);
}

struct wlb_fullscreen_shell *
wlb_fullscreen_shell_create(struct wlb_compositor *comp)
{
	struct wlb_fullscreen_shell *fshell;

	fshell = zalloc(sizeof *fshell);
	if (!fshell)
		return NULL;

	fshell->compositor = comp;
	wl_list_init(&fshell->presentation_list);
	fshell->global = wl_global_create(comp->display,
					  &wl_fullscreen_shell_interface, 1,
					  fshell, fullscreen_shell_bind);
	if (!fshell->global)
		goto err_alloc;

	return fshell;

err_alloc:
	free(fshell);
	return NULL;
}

void
wlb_fullscreen_shell_destroy(struct wlb_fullscreen_shell *fshell)
{
	struct wlb_presentation *pres, *np;

	wl_list_for_each_safe(pres, np, &fshell->presentation_list, link)
		wlb_presentation_destroy(pres);

	wl_global_destroy(fshell->global);
	free(fshell);
}

