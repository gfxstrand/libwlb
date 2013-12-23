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
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "weston-egl-ext.h"

#include "wlb-private.h"

struct gles2_shader {
	struct wl_list link;
	union {
		uint32_t format;
		struct wlb_buffer_type *type;
	};

	GLuint fshader;
	GLuint program;

	GLint va_vertex;
	GLint vu_buffer_tf;
	GLint vu_output_tf;

	union {
		GLint fu_texture;
		GLint fu_color;
	};
};

struct gles2_surface {
	struct wlb_surface *surface;
	struct wl_list link;
	struct wl_listener destroy_listener;

	int32_t width, height;
	uint32_t pitch;

	struct wl_resource *buffer;
	struct wlb_buffer_type *buffer_type;
	void *buffer_type_data;
	size_t buffer_type_size;

	struct gles2_shader *shader;

	GLuint textures[WLB_BUFFER_MAX_PLANES];
};

struct gles2_output {
	struct wlb_gles2_renderer *renderer;
	struct wl_list link;
	struct wl_listener destroy_listener;

	EGLSurface egl_surface;
};

struct wlb_gles2_renderer {
	struct wlb_compositor *compositor;

	struct wl_list surface_list;
	struct wl_list output_list;

	struct wlb_matrix output_mat;
	struct wl_array vertices;

	GLuint vertex_shader;
	struct gles2_shader *solid_shader;
	struct wl_list shm_format_shader_list;
	struct wl_list buffer_type_shader_list;

	EGLDisplay egl_display;
	EGLConfig egl_config;
	EGLContext egl_context;

	struct wlb_wayland_egl_binding *wayland_binding;

	int initialized;

	int has_unpack_subimage;
};

static const GLchar *vertex_shader_source =
"attribute highp vec2 va_vertex;\n"
"uniform highp mat3 vu_buffer_tf;\n"
"uniform highp mat3 vu_output_tf;\n"
"varying mediump vec2 vo_tex_coord;\n"
"\n"
"void main() {\n"
"	vec3 pos = vu_output_tf * vec3(va_vertex, 1);\n"
"	vo_tex_coord = (vu_buffer_tf * vec3(va_vertex, 1)).xy;\n"
"	gl_Position = vec4(pos.xy, 0, pos.z);\n"
"}\n";

static const GLchar *solid_shader_source =
"uniform lowp vec4 fu_color;\n"
"\n"
"void main() {\n"
"	gl_FragColor = fu_color;\n"
"}\n";

static const GLchar *argb8888_shader_source =
"uniform sampler2D fu_texture;\n"
"varying mediump vec2 vo_tex_coord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = vec4(texture2D(fu_texture, vo_tex_coord).bgr, 1);\n"
"}\n";

static const GLchar *xrgb8888_shader_source =
"uniform sampler2D fu_texture;\n"
"varying mediump vec2 vo_tex_coord;\n"
"\n"
"void main() {\n"
"	gl_FragColor = texture2D(fu_texture, vo_tex_coord).bgra;\n"
"}\n";

static const GLchar *buffer_type_shader_source = 
"varying mediump vec2 vo_tex_coord;\n"
"void main() {\n"
"	gl_FragColor = wlb_get_fragment_color(vo_tex_coord);\n"
"}\n";

static GLuint
shader_from_source(GLenum shader_type, GLsizei count,
		   const GLchar * const *source)
{
	GLuint shader;
	GLint info;
	GLchar *log;

	shader = glCreateShader(shader_type);
	glShaderSource(shader, count, source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &info);
	if (info != GL_TRUE) {
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info);
		log = malloc(info * sizeof(GLchar));
		if (!log) {
			wlb_error("Out of memory\n");
			glDeleteShader(shader);
			errno = ENOMEM;
			return 0;
		}
		glGetShaderInfoLog(shader, info, NULL, log);
		wlb_error("Failed to compile shader:\n%s", log);
		wlb_debug("Shader source:\n%s\n", source);
		glDeleteShader(shader);
		free(log);
		return 0;
	}

	return shader;
}

static GLuint
program_from_shaders(GLuint vertex_shader, GLuint fragment_shader)
{
	GLuint program;
	GLint info;
	GLchar *log;

	program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &info);
	if (info != GL_TRUE) {
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info);
		log = malloc(info * sizeof(GLchar));
		if (!log) {
			wlb_error("Out of memory\n");
			errno = ENOMEM;
			glDeleteProgram(program);
			return 0;
		}
		glGetProgramInfoLog(program, info, NULL, log);
		wlb_error("Failed to link shader program:\n%s", log);
		free(log);
		glDeleteProgram(program);
		return 0;
	}

	return program;
}

static void
gles2_shader_destroy(struct gles2_shader *shader)
{
	glDeleteProgram(shader->program);
	glDeleteShader(shader->fshader);
	free(shader);
}

static struct gles2_shader *
gles2_shader_get_for_source(struct wlb_gles2_renderer *r, GLsizei count,
			    const GLchar * const *source)
{
	struct gles2_shader *shader;

	if (!r->vertex_shader) {
		r->vertex_shader = shader_from_source(GL_VERTEX_SHADER, 1,
						      &vertex_shader_source);
		if (!r->vertex_shader)
			return NULL;
	}

	shader = zalloc(sizeof *shader);
	if (!shader)
		return NULL;
	
	shader->fshader = shader_from_source(GL_FRAGMENT_SHADER, count, source);
	if (!shader->fshader)
		goto err_alloc;
	
	shader->program = program_from_shaders(r->vertex_shader,
					       shader->fshader);
	if (!shader->program)
		goto err_shader;
	
	shader->va_vertex = glGetAttribLocation(shader->program, "va_vertex");
	shader->vu_buffer_tf =
		glGetUniformLocation(shader->program, "vu_buffer_tf");
	shader->vu_output_tf =
		glGetUniformLocation(shader->program, "vu_output_tf");

	return shader;

err_shader:
	glDeleteShader(shader->fshader);
err_alloc:
	free(shader);
	return NULL;
}

static struct gles2_shader *
gles2_shader_get_for_shm_format(struct wlb_gles2_renderer *r, uint32_t format)
{
	struct gles2_shader *shader;

	wl_list_for_each(shader, &r->shm_format_shader_list, link)
		if (shader->format == format)
			return shader;

	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
		shader = gles2_shader_get_for_source(r, 1,
						     &argb8888_shader_source);
		break;
	case WL_SHM_FORMAT_XRGB8888:
		shader = gles2_shader_get_for_source(r, 1,
						     &xrgb8888_shader_source);
		break;
	default:
		wlb_error("Invalid buffer format: %u", format);
		errno = EINVAL;
		return NULL;
	}

	if (!shader)
		return NULL;

	shader->fu_texture =
		glGetUniformLocation(shader->program, "fu_texture");
	
	shader->format = format;
	wl_list_insert(&r->shm_format_shader_list, &shader->link);

	return shader;
}

static struct gles2_shader *
gles2_shader_get_for_buffer_type(struct wlb_gles2_renderer *r,
				 struct wlb_buffer_type *type, void *type_data)
{
	struct gles2_shader *shader;
	GLchar const *sources[2];

	wl_list_for_each(shader, &r->buffer_type_shader_list, link)
		if (shader->type == type)
			return shader;
	
	sources[0] = type->gles2_shader;
	sources[1] = buffer_type_shader_source;
	shader = gles2_shader_get_for_source(r, 2, sources);

	if (!shader)
		return NULL;

	shader->type = type;
	wl_list_insert(&r->buffer_type_shader_list, &shader->link);

	if (type->program_linked)
		type->program_linked(type_data, shader->program);

	return shader;
}

static struct gles2_shader *
gles2_shader_get_solid(struct wlb_gles2_renderer *r)
{
	if (r->solid_shader)
		return r->solid_shader;

	r->solid_shader = gles2_shader_get_for_source(r, 1,
						      &solid_shader_source);
	if (!r->solid_shader)
		return NULL;

	r->solid_shader->fu_color =
		glGetUniformLocation(r->solid_shader->program, "fu_color");
	r->solid_shader->format = 0xffffffff;

	return r->solid_shader;
}

static void
gles2_surface_destroy(struct gles2_surface *surface)
{
	glDeleteTextures(WLB_BUFFER_MAX_PLANES, surface->textures);

	wl_list_remove(&surface->link);
	wl_list_remove(&surface->destroy_listener.link);

	free(surface);
}

static void
surface_destroy_handler(struct wl_listener *listener, void *data)
{
	struct gles2_surface *gs;
	
	gs = wl_container_of(listener, gs, destroy_listener);
	gles2_surface_destroy(gs);
}

static struct gles2_surface *
gles2_surface_get(struct wlb_gles2_renderer *gr, struct wlb_surface *surface)
{
	struct gles2_surface *gs;
	struct wl_listener *listener;

	listener = wlb_surface_get_destroy_listener(surface,
						    surface_destroy_handler);
	if (listener) {
		gs = wl_container_of(listener, gs, destroy_listener);
		return gs;
	}

	gs = zalloc(sizeof *gs);
	if (!gs)
		return NULL;
	
	gs->surface = surface;
	gs->destroy_listener.notify = surface_destroy_handler;
	wlb_surface_add_destroy_listener(surface, &gs->destroy_listener);
	wl_list_insert(&gr->surface_list, &gs->link);

	return gs;
}

static int
gles2_surface_update_shm(struct wlb_gles2_renderer *gr,
			 struct gles2_surface *gs, int full_damage)
{
	struct wlb_rectangle *drects;
	uint32_t format, stride;
	void *pixel_data;
	int i, ndrects, err = 0;

	if (full_damage) {
		drects = NULL;
	} else {
		drects = wlb_surface_get_buffer_damage(gs->surface, &ndrects);

		if (ndrects == 0)
			return 0;

		if (!drects)
			/* Failed to get damage, but we can still try and
			 * upload the entire thing */
			full_damage = 1;
	}

	pixel_data = gs->buffer_type->mmap(gs->buffer_type_data, gs->buffer,
					   &stride, &format);
	if (!pixel_data) {
		wlb_error("Failed to map buffer");
		err = 1;
		goto err_damage;
	}

	gs->pitch = stride / 4;

	gs->shader = gles2_shader_get_for_shm_format(gr, format);
	if (!gs->shader) {
		wlb_error("Failed to find shader");
		err = 1;
		goto err_mmap;
	}

	glUseProgram(gs->shader->program);

	glUniform1i(gs->shader->fu_texture, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gs->textures[0]);

#ifdef GL_EXT_unpack_subimage
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, gs->pitch);

	if (gr->has_unpack_subimage && full_damage) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gs->width, gs->height,
			     0, GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
		goto done;
	} else if (gr->has_unpack_subimage) {
		for (i = 0; i < ndrects; ++i) {
			glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, drects[i].x);
			glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, drects[i].y);
			glTexSubImage2D(GL_TEXTURE_2D, 0,
					drects[i].x, drects[i].y,
					drects[i].width, drects[i].height,
					GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);
		}
		goto done;
	}
#endif

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gs->pitch, gs->height, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, pixel_data);

done:
err_mmap:
	if (gs->buffer_type->munmap)
		gs->buffer_type->munmap(gs->buffer_type_data, gs->buffer,
					pixel_data);
err_damage:
	free(drects);

	return err ? -1 : 0;
}

static void
gles2_surface_ensure_textures(struct gles2_surface *gs, int num_textures)
{
	int i;

	if (num_textures < 0)
		num_textures = 0;

	for (i = 0; i < num_textures && i < WLB_BUFFER_MAX_PLANES; ++i) {
		if (gs->textures[0] != 0)
			continue;

		glGenTextures(1, &gs->textures[i]);

		glBindTexture(GL_TEXTURE_2D, gs->textures[i]);
		glTexParameteri(GL_TEXTURE_2D,
				GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D,
				GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D,
				GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D,
				GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	for (i = num_textures; i < WLB_BUFFER_MAX_PLANES; ++i) {
		glDeleteTextures(1, &gs->textures[i]);
	}
}

static int
gles2_surface_prepare(struct wlb_gles2_renderer *gr, struct gles2_surface *gs)
{
	int32_t width, height;
	int full_damage = 0;

	gs->buffer = wlb_surface_buffer(gs->surface);
	gs->buffer_type =
		wlb_compositor_get_buffer_type(gr->compositor, gs->buffer,
					       &gs->buffer_type_data,
					       &gs->buffer_type_size);
	if (!gs->buffer_type) {
		return -1;
	}

	gs->buffer_type->get_size(gs->buffer_type_data, gs->buffer,
				  &width, &height);

	if (width < 0 || height < 0) {
		gs->width = 0;
		gs->height = 0;
		wlb_error("Invalid buffer size");
		return -1;
	} else if (width != gs->width || height != gs->height) {
		gs->width = width;
		gs->height = height;
		full_damage = 1;
	}

	if (gs->buffer_type->gles2_shader && gs->buffer_type->attach) {
		gs->pitch = gs->width;
		gles2_surface_ensure_textures(gs, gs->buffer_type->num_planes);
		gs->shader =
			gles2_shader_get_for_buffer_type(gr, gs->buffer_type,
							 gs->buffer_type_data);
		glUseProgram(gs->shader->program);
		gs->buffer_type->attach(gs->buffer_type_data, gs->buffer,
					gs->shader->program, gs->textures);
	} else if (gs->buffer_type->mmap) {
		if (gs->textures[0] == 0)
			full_damage = 1;

		gles2_surface_ensure_textures(gs, 1);
		if (gles2_surface_update_shm(gr, gs, full_damage) < 0)
			return -1;
	} else {
		wlb_error("Buffer type is not CPU-mappable and does not proivde a GLES2 attach mechanism");
		return -1;
	}

	wlb_surface_reset_damage(gs->surface);

	return 0;
}

static void
gles2_surface_finish(struct wlb_gles2_renderer *gr, struct gles2_surface *gs)
{
	if (gs->buffer_type->detach)
		gs->buffer_type->detach(gs->buffer_type_data, gs->buffer);
}

static void
gles2_output_destroy(struct gles2_output *output)
{
	if (output->egl_surface != EGL_NO_SURFACE)
		eglDestroySurface(output->renderer->egl_display,
				  output->egl_surface);

	wl_list_remove(&output->link);
	wl_list_remove(&output->destroy_listener.link);

	free(output);
}

static void
output_destroy_handler(struct wl_listener *listener, void *data)
{
	struct gles2_output *go;
	
	go = wl_container_of(listener, go, destroy_listener);
	gles2_output_destroy(go);
}

static struct gles2_output *
gles2_output_create(struct wlb_gles2_renderer *gr, struct wlb_output *output)
{
	struct gles2_output *go;

	go = zalloc(sizeof *go);
	if (!go)
		return NULL;

	go->destroy_listener.notify = output_destroy_handler;
	wl_signal_add(&output->destroy_signal, &go->destroy_listener);

	go->renderer = gr;
	wl_list_insert(&gr->output_list, &go->link);

	return go;
}

static struct gles2_output *
gles2_output_get(struct wlb_gles2_renderer *gr, struct wlb_output *output)
{
	struct gles2_output *go;
	struct wl_listener *listener;

	listener = wl_signal_get(&output->destroy_signal,
				 output_destroy_handler);
	if (listener) {
		go = wl_container_of(listener, go, destroy_listener);
		return go;
	} else {
		return NULL;
	}
}

static void
egl_error(const char *msg)
{
	const char *err;

#define MYERRCODE(x) case x: err = #x; break;
	switch (eglGetError()) {
	MYERRCODE(EGL_SUCCESS)
	MYERRCODE(EGL_NOT_INITIALIZED)
	MYERRCODE(EGL_BAD_ACCESS)
	MYERRCODE(EGL_BAD_ALLOC)
	MYERRCODE(EGL_BAD_ATTRIBUTE)
	MYERRCODE(EGL_BAD_CONTEXT)
	MYERRCODE(EGL_BAD_CONFIG)
	MYERRCODE(EGL_BAD_CURRENT_SURFACE)
	MYERRCODE(EGL_BAD_DISPLAY)
	MYERRCODE(EGL_BAD_SURFACE)
	MYERRCODE(EGL_BAD_MATCH)
	MYERRCODE(EGL_BAD_PARAMETER)
	MYERRCODE(EGL_BAD_NATIVE_PIXMAP)
	MYERRCODE(EGL_BAD_NATIVE_WINDOW)
	MYERRCODE(EGL_CONTEXT_LOST)
	default:
		err = "unknown";
	}
#undef MYERRCODE

	wlb_error("%s: %s\n", msg, err);
}

WL_EXPORT struct wlb_gles2_renderer *
wlb_gles2_renderer_create(struct wlb_compositor *c)
{
	struct wlb_gles2_renderer *renderer;
	
	renderer = zalloc(sizeof *renderer);
	if (!renderer)
		return NULL;

	renderer->compositor = c;

	wl_list_init(&renderer->surface_list);
	wl_list_init(&renderer->output_list);

	wl_list_init(&renderer->shm_format_shader_list);
	wl_list_init(&renderer->buffer_type_shader_list);

	renderer->egl_display = EGL_NO_DISPLAY;
	renderer->egl_context = EGL_NO_CONTEXT;
	
	return renderer;
}

WL_EXPORT struct wlb_gles2_renderer *
wlb_gles2_renderer_create_for_egl(struct wlb_compositor *c,
				  EGLDisplay display, EGLConfig *user_config)
{
	struct wlb_gles2_renderer *renderer;
	const char *version;
	int major, minor;
	EGLint matched;
	EGLConfig config;
	EGLContext context;

	version = eglQueryString(display, EGL_VERSION);
	if (!version) {
		egl_error("Unable to discover EGL version");
		return NULL;
	}
	wlb_debug("EGL version: %s\n", version);

	if (sscanf(version, "%d.%d ", &major, &minor) < 2) {
		wlb_error("Unable to interpret EGL version string\n");
		return NULL;
	}

	if (major < 1 || (major == 1 && minor < 3)) {
		wlb_error("EGL version 1.3 is required for OpenGL ES 2.0\n");
		return NULL;
	}

	static const EGLint attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};

	if (user_config) {
		config = *user_config;
	} else {
		if (!eglChooseConfig(display, attribs, &config, 1, &matched) ||
		    matched < 1) {
			egl_error("Failed to chose EGL configuration");
			return NULL;
		}
	}

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		egl_error("Failed to bind EGL_OPENGL_ES_API");
		return NULL;
	}

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	context = eglCreateContext(display, config, EGL_NO_CONTEXT,
				   context_attribs);
	if (context == EGL_NO_CONTEXT) {
		egl_error("Failed to create EGL context");
		return NULL;
	}

	renderer = wlb_gles2_renderer_create(c);
	renderer->egl_display = display;
	renderer->egl_config = config;
	renderer->egl_context = context;

	return renderer;
}

WL_EXPORT void
wlb_gles2_renderer_destroy(struct wlb_gles2_renderer *gr)
{
	struct gles2_surface *surface, *sunext;
	struct gles2_output *output, *onext;
	struct gles2_shader *shader, *shnext;

	if (gr->wayland_binding)
		wlb_wayland_egl_binding_destroy(gr->wayland_binding);

	wl_list_for_each_safe(surface, sunext, &gr->surface_list, link)
		gles2_surface_destroy(surface);

	wl_list_for_each_safe(output, onext, &gr->output_list, link)
		gles2_output_destroy(output);

	gles2_shader_destroy(gr->solid_shader);
	wl_list_for_each_safe(shader, shnext, &gr->shm_format_shader_list, link)
		gles2_shader_destroy(shader);
	wl_list_for_each_safe(shader, shnext, &gr->buffer_type_shader_list, link)
		gles2_shader_destroy(shader);

	free(gr);
}

WL_EXPORT void
wlb_gles2_renderer_add_egl_output(struct wlb_gles2_renderer *gr,
				  struct wlb_output *output,
				  EGLNativeWindowType window)
{
	struct gles2_output *go;

	if (gr->egl_display == EGL_NO_DISPLAY)
		return;

	go = gles2_output_get(gr, output);
	if (go == NULL) {
		go = gles2_output_create(gr, output);
		if (go == NULL)
			return;
	} else {
		eglDestroySurface(gr->egl_display, go->egl_surface);
	}

	go->egl_surface =
		eglCreateWindowSurface(gr->egl_display, gr->egl_config,
				       window, NULL);
	if (go->egl_surface == EGL_NO_SURFACE)
		egl_error("Failed to create EGL surface");
}

static void
wlb_gles2_renderer_initialize(struct wlb_gles2_renderer *gr)
{
	const char *extensions;
	EGLDisplay egl_display;

	if (gr->initialized)
		return;	

	if (gr->egl_display == EGL_NO_DISPLAY) {
		egl_display = eglGetCurrentDisplay();
	} else {
		egl_display = gr->egl_display;
	}

	if (egl_display != EGL_NO_DISPLAY) {
		extensions = (const char *) eglQueryString(egl_display,
							   EGL_EXTENSIONS);
		wlb_debug("Available EGL Extensions:\n%s\n\n", extensions);

		if (strstr(extensions, "EGL_WL_bind_wayland_display"))
			gr->wayland_binding =
				wlb_wayland_egl_binding_create(gr->compositor,
							       egl_display);
	}
	
	extensions = (const char *) glGetString(GL_EXTENSIONS);
	wlb_debug("Available GLES 2.0 Extensions:\n%s\n\n", extensions);

#ifdef GL_EXT_unpack_subimage
	if (strstr(extensions, "GL_EXT_unpack_subimage"))
		gr->has_unpack_subimage = 1;
#endif

	gr->initialized = 1;
}

static void
make_triangles_from_region(struct wl_array *array, pixman_region32_t *region)
{
	pixman_box32_t *rects;
	int i, nrects;
	GLfloat *verts;

	rects = pixman_region32_rectangles(region, &nrects);

	for (i = 0; i < nrects; ++i) {
		verts = wl_array_add(array, 12 * sizeof(GLfloat));

		verts[ 0] = rects[i].x1; verts[ 1] = rects[i].y1;
		verts[ 2] = rects[i].x2; verts[ 3] = rects[i].y1;
		verts[ 4] = rects[i].x2; verts[ 5] = rects[i].y2;
		verts[ 6] = rects[i].x2; verts[ 7] = rects[i].y2;
		verts[ 8] = rects[i].x1; verts[ 9] = rects[i].y2;
		verts[10] = rects[i].x1; verts[11] = rects[i].y1;
	}
}

static void
paint_surface(struct wlb_gles2_renderer *gr, struct wlb_output *output)
{
	struct wlb_matrix buffer_mat;
	struct wlb_surface *surface;
	struct gles2_surface *gs;
	pixman_region32_t damage;
	int32_t sx, sy;
	uint32_t swidth, sheight;

	surface = wlb_output_surface(output);
	gs = gles2_surface_get(gr, surface);
	if (!gs)
		return;
	if (gles2_surface_prepare(gr, gs) < 0)
		return;

	glUniformMatrix3fv(gs->shader->vu_output_tf, 1, GL_FALSE,
			   gr->output_mat.d);

	wlb_matrix_init(&buffer_mat);
	if ((int32_t)gs->pitch != gs->width)
		wlb_matrix_scale(&buffer_mat, &buffer_mat,
				 gs->width / (float)gs->pitch, 1);

	wlb_output_surface_position(output, &sx, &sy, &swidth, &sheight);
	wlb_matrix_scale(&buffer_mat, &buffer_mat,
			 1 / (float)swidth, 1 / (float)sheight);
	wlb_matrix_translate(&buffer_mat, &buffer_mat, -sx, -sy);

	glUniformMatrix3fv(gs->shader->vu_buffer_tf, 1, GL_FALSE, buffer_mat.d);

	pixman_region32_init_rect(&damage, sx, sy, swidth, sheight);

	gr->vertices.size = 0;
	make_triangles_from_region(&gr->vertices, &damage);
	pixman_region32_fini(&damage);

	glVertexAttribPointer(gs->shader->va_vertex, 2, GL_FLOAT, GL_FALSE, 0,
			      gr->vertices.data);
	glEnableVertexAttribArray(gs->shader->va_vertex);
	glDrawArrays(GL_TRIANGLES, 0, gr->vertices.size / (sizeof(GLfloat)*2));
	glDisableVertexAttribArray(gs->shader->va_vertex);

	gles2_surface_finish(gr, gs);
}

WL_EXPORT void
wlb_gles2_renderer_repaint_output(struct wlb_gles2_renderer *gr,
				  struct wlb_output *output)
{
	struct gles2_output *go;

	assert(output->current_mode);

	go = gles2_output_get(gr, output);

	if (go && go->egl_surface != EGL_NO_SURFACE) {
		if (!eglMakeCurrent(gr->egl_display, go->egl_surface,
				    go->egl_surface, gr->egl_context)) {
			egl_error("Failed to make EGL context current");
			return;
		}
	}

	wlb_gles2_renderer_initialize(gr);

	glViewport(0, 0,
		   output->current_mode->width,
		   output->current_mode->height);

	wlb_matrix_ortho(&gr->output_mat, 0, output->current_mode->width,
			 0, output->current_mode->height);
	
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT);

	if (wlb_output_surface(output))
		paint_surface(gr, output);

	if (go && go->egl_surface != EGL_NO_SURFACE) {
		eglSwapBuffers(gr->egl_display, go->egl_surface);
	}
}

