#ifndef _NWL_EGL_H_
#define _NWL_EGL_H_
#include <stdbool.h>

struct nwl_surface;
typedef void* EGLSurface;

struct nwl_surface_egl {
	struct wl_egl_window *window;
	EGLSurface surface;
};

// This needs improvement, e.g. ability to request specific API etc..
bool nwl_surface_render_backend_egl(struct nwl_surface *surface);

#endif
