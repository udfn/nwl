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
	return 0;
}

static void nwl_egl_uninit(struct nwl_egl_data *egl) {
	if (egl->context) {
		eglDestroyContext(egl->display, egl->context);
	}
	if (egl->display) {
		eglTerminate(egl->display);
	}
	egl->display = NULL;
}

static void egl_sub_destroy(struct nwl_state_sub *sub) {
	struct nwl_egl_data *egl = wl_container_of(sub, egl, sub);
	nwl_egl_uninit(egl);
	free(egl);
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

void nwl_egl_surface_set_size(struct nwl_egl_surface *egl, struct nwl_surface *surface, uint32_t width, uint32_t height) {
	if (!egl->window) {
		egl->window = wl_egl_window_create(surface->wl.surface, width, height);
		egl->surface = eglCreatePlatformWindowSurfaceEXT(egl->egl->display, egl->config, egl->window, NULL);
		// Apparently setting swap interval to 0 is a good idea. So do that.
		eglMakeCurrent(egl->egl->display, egl->surface, egl->surface, egl->context);
		eglSwapInterval(egl->egl->display, 0);
		eglMakeCurrent(egl->egl->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	} else {
		wl_egl_window_resize(egl->window, width, height, 0, 0);
	}
}

static void egl_surface_destroy_surface(struct nwl_egl_surface *egl) {
	if (egl->window) {
		wl_egl_window_destroy(egl->window);
		eglDestroySurface(egl->egl->display, egl->surface);
	}
	if (egl->context) {
		if (egl->context != egl->egl->context) {
			eglDestroyContext(egl->egl->display, egl->context);
		}
	}
}

void nwl_egl_surface_destroy(struct nwl_egl_surface *egl) {
	egl_surface_destroy_surface(egl);
	free(egl);
}

bool nwl_egl_surface_global_context(struct nwl_egl_surface *egl) {
	struct nwl_egl_data *glob_egl = egl->egl;
	if (glob_egl->context) {
		egl->context = glob_egl->context;
		egl->config = glob_egl->config;
		return true;
	}
	// ugh
	if (nwl_egl_surface_create_context(egl)) {
		glob_egl->context = egl->context;
		glob_egl->config = egl->config;
		return true;
	}
	return false;
}

bool nwl_egl_surface_create_context(struct nwl_egl_surface *egl) {
	const EGLint config[] = {
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
		return false;
	}
	if (!eglChooseConfig(egl->egl->display, config, &egl->config, 1, &confignum)) {
		fprintf(stderr, "failed to choose EGL config\n");
		return false;
	}
	EGLint attribs[] = {
		EGL_NONE
	};
	egl->context = eglCreateContext(egl->egl->display, egl->config, EGL_NO_CONTEXT, attribs);
	if (!egl->context) {
		fprintf(stderr, "failed to create EGL context\n");
		return false;
	}
	return true;
}

struct nwl_egl_surface *nwl_egl_surface_create(struct nwl_state *state) {
	struct nwl_state_sub *sub = nwl_state_get_sub(state, &egl_subimpl);
	struct nwl_egl_data *egl = NULL;
	if (!sub) {
		egl = calloc(1, sizeof(struct nwl_egl_data));
		egl->sub.impl = &egl_subimpl;
		nwl_state_add_sub(state, &egl->sub);
	} else {
		egl = wl_container_of(sub, egl, sub);
	}
	if (egl->inited == 2) {
		return NULL;
	} else if (egl->inited == 0) {
		if (!nwl_egl_try_init(state, egl)) {
			return NULL;
		}
	}
	struct nwl_egl_surface *surface_egl = calloc(sizeof(struct nwl_egl_surface), 1);
	surface_egl->egl = egl;
	return surface_egl;
}
