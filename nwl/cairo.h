#ifndef _NWL_CAIRO_H
#define _NWL_CAIRO_H

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

struct nwl_cairo_surface {
	cairo_t *ctx;
	cairo_surface_t *surface;
};

struct nwl_surface;
typedef void (*nwl_surface_cairo_render_t)(struct nwl_surface *surface, struct nwl_cairo_surface *cairo_surface);
void nwl_surface_renderer_cairo(struct nwl_surface *surface, nwl_surface_cairo_render_t renderfunc, int flags);

#endif
