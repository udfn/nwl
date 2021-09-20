#include <epoxy/egl.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-egl.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"
#include "nwl/egl.h"

static char nwl_egl_init(struct nwl_state *state) {
	state->egl.display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_KHR, state->wl.display, NULL);
	EGLint major, minor;
	if (!eglInitialize(state->egl.display, &major, &minor)) {
		fprintf(stderr, "failed to init EGL\n");
		return 1;
	}

	static const EGLint config[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLint confignum = 0;
	if (!eglBindAPI(EGL_OPENGL_API)) {
		fprintf(stderr, "failed to bind OpenGL API\n");
		return 1;
	}
	if (!eglChooseConfig(state->egl.display, config, &state->egl.config, 1, &confignum)) {
		fprintf(stderr, "failed to choose EGL config\n");
		return 1;
	}
	EGLint attribs[] = {
		EGL_NONE
	};
	state->egl.context = eglCreateContext(state->egl.display, state->egl.config, EGL_NO_CONTEXT, attribs);
	if (!state->egl.context) {
		fprintf(stderr, "failed to create EGL context\n");
		return 1;
	}
	return 0;
}

void nwl_egl_uninit(struct nwl_state *state) {
	if (state->egl.context) {
		eglDestroyContext(state->egl.display, state->egl.context);
	}
	eglTerminate(state->egl.display);
}

static bool nwl_egl_try_init(struct nwl_state *state) {
	if (nwl_egl_init(state)) {
		nwl_egl_uninit(state);
		state->egl.inited = 2;
		return false;
	}
	state->egl.inited = 1;
	return true;
}

static void egl_surface_destroy(struct nwl_surface *surface) {
	struct nwl_surface_egl *egl = surface->render_backend.data;
	if (egl->window) {
		wl_egl_window_destroy(egl->window);
		eglDestroySurface(surface->state->egl.display, egl->surface);
	}
	free(surface->render_backend.data);
}

static void egl_surface_applysize(struct nwl_surface *surface) {
	struct nwl_surface_egl *egl = surface->render_backend.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	if (!egl->window) {
		egl->window = wl_egl_window_create(surface->wl.surface, scaled_width, scaled_height);
		egl->surface = eglCreatePlatformWindowSurfaceEXT(surface->state->egl.display, surface->state->egl.config, egl->window, NULL);
		surface->render.impl->surface_create(surface, scaled_width, scaled_height);
	} else {
		wl_egl_window_resize(egl->window, scaled_width, scaled_height, 0, 0);
		surface->render.impl->surface_set_size(surface, scaled_width, scaled_height);
	}
}

static void egl_surface_swapbuffers(struct nwl_surface *surface) {
	surface->render.impl->swap_buffers(surface);
}

static void egl_surface_destroy_surface(struct nwl_surface *surface) {
	struct nwl_surface_egl *egl = surface->render_backend.data;
	surface->render.impl->surface_destroy(surface);
	if (egl->window) {
		wl_egl_window_destroy(egl->window);
		eglDestroySurface(surface->state->egl.display, egl->surface);
		egl->window = NULL;
	}
}

static struct nwl_render_backend_impl egl_backend = {
	egl_surface_swapbuffers,
	egl_surface_applysize,
	egl_surface_destroy,
	egl_surface_destroy_surface
};

bool nwl_surface_render_backend_egl(struct nwl_surface *surface) {
	if (surface->state->egl.inited == 2) {
		return false;
	} else if (surface->state->egl.inited == 0) {
		if (!nwl_egl_try_init(surface->state)) {
			return false;
		}
	}
	surface->render_backend.data = calloc(sizeof(struct nwl_surface_egl), 1);
	surface->render_backend.impl = &egl_backend;
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	return true;
}
