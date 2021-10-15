#ifndef _NWL_EGL_H_
#define _NWL_EGL_H_
#include <stdbool.h>
#include <EGL/egl.h>

struct nwl_state;
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
struct nwl_surface_egl *nwl_egl_surface_create(struct nwl_state *state);
void nwl_egl_surface_set_size(struct nwl_surface_egl *egl, struct nwl_surface *surface, uint32_t width, uint32_t height);
void nwl_surface_egl_destroy(struct nwl_surface_egl *egl);

#endif
