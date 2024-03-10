#ifndef _NWL_CAIRO_H
#define _NWL_CAIRO_H
#include "shm.h"
#include <stdbool.h>

typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;
struct nwl_surface;
struct wl_surface;

struct nwl_cairo_surface {
	cairo_t *ctx;
	cairo_surface_t *surface;
	bool rerender;
};

struct nwl_cairo_renderer {
	struct nwl_shm_bufferman shm;
	struct nwl_cairo_surface cairo_surfaces[NWL_SHM_BUFFERMAN_MAX_BUFFERS];
	int next_buffer;
	int prev_buffer;
};

void nwl_cairo_renderer_init(struct nwl_cairo_renderer *renderer);
void nwl_cairo_renderer_finish(struct nwl_cairo_renderer *renderer);
void nwl_cairo_renderer_submit(struct nwl_cairo_renderer *renderer, struct nwl_surface *surface, int32_t x, int32_t y);
struct nwl_cairo_surface *nwl_cairo_renderer_get_surface(struct nwl_cairo_renderer *renderer, struct nwl_surface *surface, bool copyprevious);

#endif
