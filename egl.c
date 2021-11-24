#include <epoxy/egl.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-egl.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"
#include "nwl/egl.h"

static char nwl_egl_init(struct nwl_state *state, struct nwl_egl_data *data) {
	data->display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_KHR, state->wl.display, NULL);
	EGLint major, minor;
	if (!eglInitialize(data->display, &major, &minor)) {
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
	if (!eglChooseConfig(data->display, config, &data->config, 1, &confignum)) {
		fprintf(stderr, "failed to choose EGL config\n");
		return 1;
	}
	EGLint attribs[] = {
		EGL_NONE
	};
	data->context = eglCreateContext(data->display, data->config, EGL_NO_CONTEXT, attribs);
	if (!data->context) {
		fprintf(stderr, "failed to create EGL context\n");
		return 1;
	}
	return 0;
}

static void nwl_egl_uninit(struct nwl_egl_data *egl) {
	if (egl->context) {
		eglDestroyContext(egl->display, egl->context);
	}
	if (egl->display) {
		eglTerminate(egl->display);
	}
	egl->context = NULL;
	egl->display = NULL;
}

static void egl_sub_destroy(void *data) {
	struct nwl_egl_data *egl = data;
	nwl_egl_uninit(egl);
	free(data);
}

static const struct nwl_state_sub_impl egl_subimpl = {
	egl_sub_destroy
};

static bool nwl_egl_try_init(struct nwl_state *state, struct nwl_egl_data *egl) {
	if (nwl_egl_init(state, egl)) {
		nwl_egl_uninit(egl);
		egl->inited = 2;
		return false;
	}
	egl->inited = 1;
	return true;
}

void nwl_egl_surface_set_size(struct nwl_surface_egl *egl, struct nwl_surface *surface, uint32_t width, uint32_t height) {
	if (!egl->window) {
		egl->window = wl_egl_window_create(surface->wl.surface, width, height);
		egl->surface = eglCreatePlatformWindowSurfaceEXT(egl->egl->display, egl->egl->config, egl->window, NULL);
	} else {
		wl_egl_window_resize(egl->window, width, height, 0, 0);
	}
}

static void egl_surface_destroy_surface(struct nwl_surface_egl *egl) {
	if (egl->window) {
		wl_egl_window_destroy(egl->window);
		eglDestroySurface(egl->egl->display, egl->surface);
		egl->window = NULL;
	}
}

void nwl_surface_egl_destroy(struct nwl_surface_egl *egl) {
	egl_surface_destroy_surface(egl);
	free(egl);
}

struct nwl_surface_egl *nwl_egl_surface_create(struct nwl_state *state) {
	struct nwl_egl_data *egl = nwl_state_get_sub(state, &egl_subimpl);
	if (!egl) {
		egl = calloc(1, sizeof(struct nwl_egl_data));
		nwl_state_add_sub(state, &egl_subimpl, egl);
	}
	if (egl->inited == 2) {
		return NULL;
	} else if (egl->inited == 0) {
		if (!nwl_egl_try_init(state, egl)) {
			return NULL;
		}
	}
	struct nwl_surface_egl *surface_egl = calloc(sizeof(struct nwl_surface_egl), 1);
	surface_egl->egl = egl;
	return surface_egl;
}
