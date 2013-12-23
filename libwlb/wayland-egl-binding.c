/*
 * Copyright © 2012 Intel Corporation
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

#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "weston-egl-ext.h"

#include "wlb-private.h"

struct wlb_wayland_egl_binding;

struct wayland_buffer_type {
	struct wlb_wayland_egl_binding *binding;

	struct wlb_buffer_type type;

	GLenum tex_target;
	GLuint tex_uniforms[3];
};

struct wayland_buffer {
	struct wl_list link;
	struct wl_resource *buffer;
	int ref_count;

	EGLImageKHR images[3];
};

struct wlb_wayland_egl_binding {
	struct wlb_compositor *compositor;

	EGLDisplay egl_display;

	struct wl_list buffer_list;

	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
	PFNEGLCREATEIMAGEKHRPROC create_image;
	PFNEGLDESTROYIMAGEKHRPROC destroy_image;

	PFNEGLBINDWAYLANDDISPLAYWL bind_display;
	PFNEGLUNBINDWAYLANDDISPLAYWL unbind_display;
	PFNEGLQUERYWAYLANDBUFFERWL query_buffer;

	struct wayland_buffer_type type_rgba;
	struct wayland_buffer_type type_external;
	struct wayland_buffer_type type_y_uv;
	struct wayland_buffer_type type_y_u_v;
	struct wayland_buffer_type type_y_xuxv;
};

static int
is_wayland_rgba(void *data, struct wl_resource *buffer)
{
	struct wayland_buffer_type *type = data;
	EGLint format;
	EGLBoolean success;

	success = type->binding->query_buffer(type->binding->egl_display,
					      (void *) buffer,
					      EGL_TEXTURE_FORMAT, &format);
	return success &&
	       (format == EGL_TEXTURE_RGB || format == EGL_TEXTURE_RGBA);
}

static int
is_wayland_external(void *data, struct wl_resource *buffer)
{
	struct wayland_buffer_type *type = data;
	EGLint format;
	EGLBoolean success;

	success = type->binding->query_buffer(type->binding->egl_display,
					      (void *) buffer,
					      EGL_TEXTURE_FORMAT, &format);
	return success && format == EGL_TEXTURE_EXTERNAL_WL;
}

static int
is_wayland_y_uv(void *data, struct wl_resource *buffer)
{
	struct wayland_buffer_type *type = data;
	EGLint format;
	EGLBoolean success;

	success = type->binding->query_buffer(type->binding->egl_display,
					      (void *) buffer,
					      EGL_TEXTURE_FORMAT, &format);
	return success && format == EGL_TEXTURE_Y_UV_WL;
}

static int
is_wayland_y_u_v(void *data, struct wl_resource *buffer)
{
	struct wayland_buffer_type *type = data;
	EGLint format;
	EGLBoolean success;

	success = type->binding->query_buffer(type->binding->egl_display,
					      (void *) buffer,
					      EGL_TEXTURE_FORMAT, &format);
	return success && format == EGL_TEXTURE_Y_U_V_WL;
}

static int
is_wayland_y_xuxv(void *data, struct wl_resource *buffer)
{
	struct wayland_buffer_type *type = data;
	EGLint format;
	EGLBoolean success;

	success = type->binding->query_buffer(type->binding->egl_display,
					      (void *) buffer,
					      EGL_TEXTURE_FORMAT, &format);
	return success && format == EGL_TEXTURE_Y_XUXV_WL;
}

static void
get_size(void *data, struct wl_resource *buffer,
	 int32_t *width, int32_t *height)
{
	struct wayland_buffer_type *type = data;
	EGLint twidth, theight;

	type->binding->query_buffer(type->binding->egl_display, (void *) buffer,
	     			    EGL_WIDTH, &twidth);
	type->binding->query_buffer(type->binding->egl_display, (void *) buffer,
				    EGL_HEIGHT, &theight);
	*width = twidth;
	*height = theight;
}

static void
program_linked(void *data, GLuint program)
{
	struct wayland_buffer_type *type = data;

	type->tex_uniforms[0] = glGetUniformLocation(program, "tex");
	type->tex_uniforms[1] = glGetUniformLocation(program, "tex1");
	type->tex_uniforms[2] = glGetUniformLocation(program, "tex2");
}

static struct wayland_buffer *
wayland_buffer_get(struct wayland_buffer_type *type,
		   struct wl_resource *buffer_res, int create)
{
	struct wayland_buffer *buffer;
	EGLint attribs[3];
	int i;

	wl_list_for_each(buffer, &type->binding->buffer_list, link)
		if (buffer->buffer == buffer_res)
			return buffer;
	
	if (!create)
		return NULL;
	
	buffer = zalloc(sizeof *buffer);
	if (!buffer)
		return NULL;
	
	buffer->buffer = buffer_res;
	buffer->ref_count = 0;

	for (i = 0; i < type->type.num_planes; ++i) {
		attribs[0] = EGL_WAYLAND_PLANE_WL;
		attribs[1] = i;
		attribs[2] = EGL_NONE;

		buffer->images[i] =
			type->binding->create_image(type->binding->egl_display,
						    NULL, EGL_WAYLAND_BUFFER_WL,
						    buffer_res, attribs);
	}

	wl_list_insert(&type->binding->buffer_list, &buffer->link);

	return buffer;
}

static void
attach(void *data, struct wl_resource *buffer_res,
       GLuint program, GLuint textures[])
{
	struct wayland_buffer_type *type = data;
	struct wayland_buffer *buffer;
	int i;

	buffer = wayland_buffer_get(type, buffer_res, 1);

	buffer->ref_count++;

	for (i = 0; i < type->type.num_planes; ++i) {
		glUniform1i(type->tex_uniforms[i], i);
		glActiveTexture(GL_TEXTURE0 + i);
		glBindTexture(type->tex_target, textures[i]);
		type->binding->image_target_texture_2d(type->tex_target,
						       buffer->images[i]);
	}
}

static void
detach(void *data, struct wl_resource *buffer_res)
{
	struct wayland_buffer_type *type = data;
	struct wayland_buffer *buffer;
	int i;
	
	buffer = wayland_buffer_get(type, buffer_res, 0);
	if (!buffer)
		return;
	
	buffer->ref_count--;
	
	if (buffer->ref_count > 0)
		return;

	for (i = 0; i < type->type.num_planes; i++)
		type->binding->destroy_image(type->binding->egl_display,
					     buffer->images[i]);
}

static const struct wlb_buffer_type buffer_type_rgba = {
	is_wayland_rgba, get_size,
	NULL, NULL,
"uniform sampler2D tex;\n"
"lowp vec4 wlb_get_fragment_color(mediump vec2 coords)\n"
"{\n"
"	return texture2D(tex, coords)\n;"
"}\n", 1,
	program_linked, attach, detach
};

static const struct wlb_buffer_type buffer_type_external = {
	is_wayland_external, get_size,
	NULL, NULL,
"#extension GL_OES_EGL_image_external : require\n"
"uniform samplerExternalOES tex;\n"
"lowp vec4 wlb_get_fragment_color(mediump vec2 coords)\n"
"{\n"
"	return texture2D(tex, coords)\n;"
"}\n", 1,
	program_linked, attach, detach
};

/* Declare common fragment shader uniforms */
#define FRAGMENT_CONVERT_YUV						\
	"	float r = y + 1.59602678 * v;\n"			\
	"	float g = y - 0.39176229 * u - 0.81296764 * v;\n"	\
	"	float b = y + 2.01723214 * u;\n"			\
	"	return vec4(r, g, b, 1);\n"

static const struct wlb_buffer_type buffer_type_y_uv = {
	is_wayland_y_uv, get_size,
	NULL, NULL,
"#extension GL_OES_EGL_image_external : require\n"
"uniform sampler2D tex;\n"
"uniform sampler2D tex1;\n"
"lowp vec4 wlb_get_fragment_color(mediump vec2 coords)\n"
"{\n"
"	float y = 1.16438356 * (texture2D(tex, coords).r - 0.0625);\n"
"	float u = texture2D(tex1, v_texcoord).r - 0.5;\n"
"	float v = texture2D(tex1, v_texcoord).g - 0.5;\n"
FRAGMENT_CONVERT_YUV
"}\n", 2,
	program_linked, attach, detach
};

static const struct wlb_buffer_type buffer_type_y_u_v = {
	is_wayland_y_u_v, get_size,
	NULL, NULL,
"#extension GL_OES_EGL_image_external : require\n"
"uniform sampler2D tex;\n"
"uniform sampler2D tex1;\n"
"uniform sampler2D tex2;\n"
"lowp vec4 wlb_get_fragment_color(mediump vec2 coords)\n"
"{\n"
"	float y = 1.16438356 * (texture2D(tex, coords).r - 0.0625);\n"
"	float u = texture2D(tex1, v_texcoord).r - 0.5;\n"
"	float v = texture2D(tex2, v_texcoord).r - 0.5;\n"
FRAGMENT_CONVERT_YUV
"}\n", 3,
	program_linked, attach, detach
};

static const struct wlb_buffer_type buffer_type_y_xuxv = {
	is_wayland_y_xuxv, get_size,
	NULL, NULL,
"#extension GL_OES_EGL_image_external : require\n"
"uniform sampler2D tex;\n"
"uniform sampler2D tex1;\n"
"lowp vec4 wlb_get_fragment_color(mediump vec2 coords)\n"
"{\n"
"	float y = 1.16438356 * (texture2D(tex, v_texcoord).r - 0.0625);\n"
"	float u = texture2D(tex1, v_texcoord).g - 0.5;\n"
"	float v = texture2D(tex1, v_texcoord).a - 0.5;\n"
FRAGMENT_CONVERT_YUV
"}\n", 2,
	program_linked, attach, detach
};

WL_EXPORT struct wlb_wayland_egl_binding *
wlb_wayland_egl_binding_create(struct wlb_compositor *comp,
			       EGLDisplay display)
{
	const char *extensions;
	struct wlb_wayland_egl_binding *binding;
	struct wl_display *wl_display;

	extensions = (const char *) eglQueryString(display,
						   EGL_EXTENSIONS);
	if (!strstr(extensions, "EGL_WL_bind_wayland_display")) {
		wlb_error("EGL_WL_bind_wayland_display not supported\n");
		return NULL;
	}

	binding = zalloc(sizeof *binding);
	if (!binding)
		return NULL;
	
	binding->compositor = comp;
	binding->egl_display = display;

	wl_list_init(&binding->buffer_list);

	binding->image_target_texture_2d =
		(void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");
	binding->create_image =
		(void *) eglGetProcAddress("eglCreateImageKHR");
	binding->destroy_image =
		(void *) eglGetProcAddress("eglDestroyImageKHR");

	wl_display = wlb_compositor_get_display(binding->compositor);
	binding->bind_display =
		(void *) eglGetProcAddress("eglBindWaylandDisplayWL");
	binding->unbind_display =
		(void *) eglGetProcAddress("eglUnbindWaylandDisplayWL");
	binding->query_buffer =
		(void *) eglGetProcAddress("eglQueryWaylandBufferWL");

	if (!binding->bind_display(binding->egl_display, wl_display)) {
		wlb_warn("Failed to bind EGL to Wayland display");
		free(binding);
		return NULL;
	}

	binding->type_rgba.binding = binding;
	binding->type_rgba.type = buffer_type_rgba;
	binding->type_rgba.tex_target = GL_TEXTURE_2D;
	wlb_compositor_add_buffer_type(comp, &binding->type_rgba.type,
				       &binding->type_rgba);

	binding->type_external.binding = binding;
	binding->type_external.type = buffer_type_external;
	binding->type_external.tex_target = GL_TEXTURE_EXTERNAL_OES;
	wlb_compositor_add_buffer_type(comp, &binding->type_external.type,
				       &binding->type_external);

	binding->type_y_uv.binding = binding;
	binding->type_y_uv.type = buffer_type_y_uv;
	binding->type_y_uv.tex_target = GL_TEXTURE_2D;
	wlb_compositor_add_buffer_type(comp, &binding->type_y_uv.type,
				       &binding->type_y_uv);

	binding->type_y_u_v.binding = binding;
	binding->type_y_u_v.type = buffer_type_y_u_v;
	binding->type_y_u_v.tex_target = GL_TEXTURE_2D;
	wlb_compositor_add_buffer_type(comp, &binding->type_y_u_v.type,
				       &binding->type_y_u_v);

	binding->type_y_xuxv.binding = binding;
	binding->type_y_xuxv.type = buffer_type_y_xuxv;
	binding->type_y_xuxv.tex_target = GL_TEXTURE_2D;
	wlb_compositor_add_buffer_type(comp, &binding->type_y_xuxv.type,
				       &binding->type_y_xuxv);

	return binding;
}

WL_EXPORT void
wlb_wayland_egl_binding_destroy(struct wlb_wayland_egl_binding *binding)
{
	struct wl_display *wl_display;

	wl_display = wlb_compositor_get_display(binding->compositor);
	binding->unbind_display(binding->egl_display, wl_display);

	free(binding);
}
