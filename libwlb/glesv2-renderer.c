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

#include "wlb-private.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

struct gles2_shader {
	struct wl_list link;

	uint32_t format;
	GLuint fshader;
	GLuint program;

	GLint va_vertex;
	GLint vu_buffer_tf;
	GLint vu_output_tf;
	GLint fu_texture;
	GLint fu_color;
};

struct gles2_surface {
	struct wlb_surface *surface;
	struct wl_list link;
	struct wl_listener destroy_listener;

	int32_t width, height, stride;
	uint32_t format;

	GLuint texture;
};

struct gles2_output {
	struct wlb_gles2_renderer *renderer;
	struct wl_list link;
	struct wl_listener destroy_listener;

	EGLSurface egl_surface;
};

struct wlb_gles2_renderer {
	struct wl_list surface_list;
	struct wl_list output_list;

	struct wlb_matrix output_mat;
	struct wl_array vertices;

	GLuint vertex_shader;
	struct gles2_shader *solid_shader;
	struct wl_list shader_list;

	EGLDisplay egl_display;
	EGLConfig egl_config;
	EGLContext egl_context;
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

static GLuint
shader_from_source(GLenum shader_type, const GLchar *source)
{
	GLuint shader;
	GLint info;
	GLchar *log;

	shader = glCreateShader(shader_type);
	glShaderSource(shader, 1, &source, NULL);
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
gles2_shader_get_for_source(struct wlb_gles2_renderer *r, const GLchar *source)
{
	struct gles2_shader *shader;

	if (!r->vertex_shader) {
		r->vertex_shader = shader_from_source(GL_VERTEX_SHADER,
						      vertex_shader_source);
		if (!r->vertex_shader)
			return NULL;
	}

	shader = zalloc(sizeof *shader);
	if (!shader)
		return NULL;
	
	shader->fshader = shader_from_source(GL_FRAGMENT_SHADER, source);
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
	shader->fu_texture =
		glGetUniformLocation(shader->program, "fu_texture");
	shader->fu_color = glGetUniformLocation(shader->program, "fu_color");

	return shader;

err_shader:
	glDeleteShader(shader->fshader);
err_alloc:
	free(shader);
	return NULL;
}

static struct gles2_shader *
gles2_shader_get_for_format(struct wlb_gles2_renderer *r, uint32_t format)
{
	struct gles2_shader *shader;

	wl_list_for_each(shader, &r->shader_list, link)
		if (shader->format == format)
			return shader;

	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
		shader = gles2_shader_get_for_source(r, argb8888_shader_source);
		break;
	case WL_SHM_FORMAT_XRGB8888:
		shader = gles2_shader_get_for_source(r, xrgb8888_shader_source);
		break;
	default:
		wlb_error("Invalid buffer format: %u", format);
		errno = EINVAL;
		return NULL;
	}

	if (!shader)
		return NULL;
	
	shader->format = format;
	wl_list_insert(&r->shader_list, &shader->link);

	return shader;
}

static struct gles2_shader *
gles2_shader_get_solid(struct wlb_gles2_renderer *r)
{
	if (r->solid_shader)
		return r->solid_shader;

	r->solid_shader = gles2_shader_get_for_source(r, solid_shader_source);
	if (!r->solid_shader)
		return NULL;

	r->solid_shader->format = 0xffffffff;

	return r->solid_shader;
}

static void
gles2_surface_destroy(struct gles2_surface *surface)
{
	glDeleteTextures(1, &surface->texture);

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

static void
gles2_surface_flush_damage(struct gles2_surface *gs)
{
	struct wl_resource *buffer;
	struct wl_shm_buffer *shm_buffer;
	pixman_region32_t damage;

	pixman_region32_init(&damage);

	buffer = wlb_surface_buffer(gs->surface);

	if (!buffer) {
		if (gs->texture)
			glDeleteTextures(1, &gs->texture);
		gs->texture = 0;
		return;
	}

	/* Make sure we have a texture */
	if (!gs->texture)
		glGenTextures(1, &gs->texture);

	shm_buffer = wl_shm_buffer_get(buffer);

	assert(shm_buffer);

	gs->width = wl_shm_buffer_get_width(shm_buffer);
	gs->height = wl_shm_buffer_get_height(shm_buffer);
	gs->stride = wl_shm_buffer_get_stride(shm_buffer) / 4;
	gs->format = wl_shm_buffer_get_format(shm_buffer);

	glBindTexture(GL_TEXTURE_2D, gs->texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gs->stride, gs->height, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE,
		     wl_shm_buffer_get_data(shm_buffer));
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

	wl_list_init(&renderer->surface_list);
	wl_list_init(&renderer->output_list);

	wl_list_init(&renderer->shader_list);

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

	wl_list_for_each_safe(surface, sunext, &gr->surface_list, link)
		gles2_surface_destroy(surface);

	wl_list_for_each_safe(output, onext, &gr->output_list, link)
		gles2_output_destroy(output);

	gles2_shader_destroy(gr->solid_shader);
	wl_list_for_each_safe(shader, shnext, &gr->shader_list, link)
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

	struct gles2_shader *shader;

	surface = wlb_output_surface(output);
	gs = gles2_surface_get(gr, surface);
	if (!gs)
		return;
	gles2_surface_flush_damage(gs);

	shader = gles2_shader_get_for_format(gr, gs->format);
	if (!shader)
		return;

	/* XXX */
	glUniform4f(shader->fu_color, 1, 0, 0, 1);

	glUseProgram(shader->program);
	glUniformMatrix3fv(shader->vu_output_tf, 1, GL_FALSE, gr->output_mat.d);

	wlb_matrix_init(&buffer_mat);
	if (gs->stride != gs->width)
		wlb_matrix_scale(&buffer_mat, &buffer_mat,
				 gs->width / (float)gs->stride, 1);
	wlb_matrix_scale(&buffer_mat, &buffer_mat,
			 1 / (float)output->surface.position.width,
			 1 / (float)output->surface.position.height);
	wlb_matrix_translate(&buffer_mat, &buffer_mat,
			     -output->surface.position.x,
			     -output->surface.position.y);
	glUniformMatrix3fv(shader->vu_buffer_tf, 1, GL_FALSE, buffer_mat.d);

	glUniform1i(shader->fu_texture, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gs->texture);
	
	pixman_region32_init_rect(&damage,
				  output->surface.position.x,
				  output->surface.position.y,
				  output->surface.position.width,
				  output->surface.position.height);
	
	gr->vertices.size = 0;
	make_triangles_from_region(&gr->vertices, &damage);
	pixman_region32_fini(&damage);

	glVertexAttribPointer(shader->va_vertex, 2, GL_FLOAT, GL_FALSE, 0,
			      gr->vertices.data);
	glEnableVertexAttribArray(shader->va_vertex);
	glDrawArrays(GL_TRIANGLES, 0, gr->vertices.size / (sizeof(GLfloat)*2));
	glDisableVertexAttribArray(shader->va_vertex);
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

