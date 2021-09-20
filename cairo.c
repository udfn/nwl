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

static void nwl_cairo_set_size(struct nwl_surface *surface, uint32_t scaled_width, uint32_t scaled_height) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	cairo_gl_surface_set_size(c->surface, scaled_width, scaled_height);
}

static void nwl_cairo_create(struct nwl_surface *surface, uint32_t scaled_width, uint32_t scaled_height) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->shm) {
		struct nwl_surface_shm *shm = surface->render_backend.data;
		if (c->surface) {
			cairo_surface_destroy(c->surface);
		}
		c->surface = cairo_image_surface_create_for_data(shm->data, CAIRO_FORMAT_ARGB32, scaled_width, scaled_height, shm->stride);
	} else {
		struct nwl_surface_egl *egl = surface->render_backend.data;
		struct nwl_cairo_state_data *statedata = nwl_state_get_sub(surface->state, &cairo_subimpl);
		if (!statedata) {
			statedata = calloc(1, sizeof(struct nwl_cairo_state_data));
			nwl_state_add_sub(surface->state, &cairo_subimpl, statedata);
		}
		if (!statedata->cairo_dev) {
			statedata->cairo_dev = cairo_egl_device_create(surface->state->egl.display, surface->state->egl.context);
		}
		c->surface = cairo_gl_surface_create_for_egl(statedata->cairo_dev, egl->surface, scaled_width, scaled_height);
	}
}

static void nwl_cairo_surface_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->surface) {
		cairo_surface_destroy(c->surface);
	}
	c->surface = NULL;
}

static void nwl_cairo_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->surface) {
		cairo_surface_destroy(c->surface);
	}
	surface->render_backend.impl->destroy(surface);
	free(surface->render.data);
}

static void nwl_cairo_swap_buffers(struct nwl_surface *surface) {
	cairo_gl_surface_swapbuffers(((struct nwl_cairo_renderer_data*)surface->render.data)->surface);
}

static int nwl_cairo_get_stride(enum wl_shm_format format, uint32_t width) {
	if (format != WL_SHM_FORMAT_ARGB8888) {
		fprintf(stderr, "wl_shm_format not ARGB8888!!");
	}
	return cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
}

static void nwl_cairo_render(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	c->renderfunc(surface, c->surface);
}

static struct nwl_renderer_impl cairo_impl = {
	nwl_cairo_get_stride,
	nwl_cairo_create,
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
	if (!dat->shm) {
		if (nwl_surface_render_backend_egl(surface)) {
			return;
		}
	}
	dat->shm = true;
	nwl_surface_render_backend_shm(surface);
}
