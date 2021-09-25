#ifndef _NWL_EGL_H_
#define _NWL_EGL_H_
#include <stdbool.h>
#include <EGL/egl.h>

struct nwl_surface;

struct nwl_egl_data {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;
	char inited;
};

struct nwl_surface_egl {
	struct wl_egl_window *window;
	EGLSurface surface;
	struct nwl_egl_data *egl;
};

// This needs improvement, e.g. ability to request specific API etc..
bool nwl_surface_render_backend_egl(struct nwl_surface *surface);

#endif
