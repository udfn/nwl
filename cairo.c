#include <cairo.h>
#include <cairo-gl.h>
#include <stdlib.h>
#include <stdio.h>
#include "nwl/cairo.h"
#include "nwl/egl.h"
#include "nwl/nwl.h"
#include "nwl/shm.h"
#include "nwl/surface.h"

struct nwl_cairo_renderer_data {
	cairo_surface_t *surface;
	nwl_surface_cairo_render_t renderfunc;
	bool shm;
	uint32_t stride;
	union {
		struct nwl_surface_shm *shm;
		struct nwl_surface_egl *egl;
	} backend;
};

struct nwl_cairo_state_data {
	cairo_device_t *cairo_dev;
};

static void nwl_cairo_state_destroy(void *data) {
	struct nwl_cairo_state_data *cairodata = data;
	if (cairodata->cairo_dev) {
		cairo_device_destroy(cairodata->cairo_dev);
	}
	free(cairodata);
}

static struct nwl_state_sub_impl cairo_subimpl = {
	nwl_cairo_state_destroy
};

static void nwl_cairo_set_size(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	if (c->shm) {
		if (c->surface) {
			cairo_surface_destroy(c->surface);
		}
		c->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, scaled_width);
		nwl_shm_set_size(c->backend.shm, surface->state, (c->stride * scaled_height));
		c->surface = cairo_image_surface_create_for_data(c->backend.shm->data, CAIRO_FORMAT_ARGB32, scaled_width, scaled_height, c->stride);
	} else {
		nwl_egl_surface_set_size(c->backend.egl, surface, scaled_width, scaled_height);
		if (!c->surface) {
			struct nwl_cairo_state_data *statedata = nwl_state_get_sub(surface->state, &cairo_subimpl);
			if (!statedata) {
				statedata = calloc(1, sizeof(struct nwl_cairo_state_data));
				nwl_state_add_sub(surface->state, &cairo_subimpl, statedata);
			}
			if (!statedata->cairo_dev) {
				statedata->cairo_dev = cairo_egl_device_create(c->backend.egl->egl->display, c->backend.egl->egl->context);
			}
			c->surface = cairo_gl_surface_create_for_egl(statedata->cairo_dev, c->backend.egl->surface, scaled_width, scaled_height);
		}
		cairo_gl_surface_set_size(c->surface, scaled_width, scaled_height);
	}
}

static void nwl_cairo_surface_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->surface) {
		cairo_surface_destroy(c->surface);
	}
	c->surface = NULL;
	// Hack.. maybe it would be better to just get rid of surface_destroy?
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
}

static void nwl_cairo_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->surface) {
		cairo_surface_destroy(c->surface);
	}
	if (c->shm) {
		nwl_shm_destroy(c->backend.shm);
	} else {
		nwl_surface_egl_destroy(c->backend.egl);
	}
	free(surface->render.data);
}

static void nwl_cairo_swap_buffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->shm) {
		uint32_t scaled_width = surface->width * surface->scale;
		uint32_t scaled_height = surface->height * surface->scale;
		struct wl_buffer *buf = nwl_shm_get_buffer(c->backend.shm, scaled_width,
				scaled_height, c->stride, WL_SHM_FORMAT_ARGB8888);
		wl_surface_attach(surface->wl.surface, buf, x, y);
		wl_surface_commit(surface->wl.surface);
	} else {
		// x y for egl surfaces how?
		cairo_gl_surface_swapbuffers(c->surface);
	}
}

static void nwl_cairo_render(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	c->renderfunc(surface, c->surface);
}

static struct nwl_renderer_impl cairo_impl = {
	nwl_cairo_set_size,
	nwl_cairo_surface_destroy,
	nwl_cairo_swap_buffers,
	nwl_cairo_render,
	nwl_cairo_destroy
};

void nwl_surface_renderer_cairo(struct nwl_surface *surface, bool egl, nwl_surface_cairo_render_t renderfunc) {
	if (surface->render.impl) {
		surface->render.impl->destroy(surface);
	}
	surface->render.impl = &cairo_impl;
	surface->render.data = calloc(1, sizeof(struct nwl_cairo_renderer_data));
	struct nwl_cairo_renderer_data *dat = surface->render.data;
	dat->renderfunc = renderfunc;
	dat->shm = !egl;
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	if (!dat->shm) {
		struct nwl_surface_egl *egl = nwl_egl_surface_create(surface->state);
		if (egl) {
			dat->backend.egl = egl;
			return;
		}
	}
	dat->shm = true;
	dat->backend.shm = nwl_shm_create();
}
