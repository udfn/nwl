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

struct nwl_egl_surface {
	struct wl_egl_window *window;
	EGLSurface surface;
	EGLConfig config;
	EGLContext context;
	struct nwl_egl_data *egl;
};

// This needs improvement, e.g. ability to request specific API etc..
struct nwl_egl_surface *nwl_egl_surface_create(struct nwl_state *state);
bool nwl_egl_surface_create_context(struct nwl_egl_surface *egl);
bool nwl_egl_surface_global_context(struct nwl_egl_surface *egl);
void nwl_egl_surface_set_size(struct nwl_egl_surface *egl, struct nwl_surface *surface, uint32_t width, uint32_t height);
void nwl_egl_surface_destroy(struct nwl_egl_surface *egl);

#endif
