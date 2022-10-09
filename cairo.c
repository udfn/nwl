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
	cairo_surface_t *egl_surface;
	nwl_surface_cairo_render_t renderfunc;
	bool shm;
	struct nwl_shm_buffer *next_buffer;
	union {
		struct nwl_shm_bufferman *shm;
		struct nwl_egl_surface *egl;
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

static const struct nwl_state_sub_impl cairo_subimpl = {
	nwl_cairo_state_destroy
};

static void nwl_cairo_set_size(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	if (c->shm) {
		// If a buffer is queued up release it.
		// This should probably be more automagic..
		if (c->next_buffer) {
			nwl_shm_buffer_release(c->next_buffer);
			c->next_buffer = NULL;
		}
		nwl_shm_bufferman_resize(c->backend.shm, surface->state, scaled_width, scaled_height,
				cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, scaled_width), WL_SHM_FORMAT_ARGB8888);
	} else {
		nwl_egl_surface_set_size(c->backend.egl, surface, scaled_width, scaled_height);
		if (!c->egl_surface) {
			struct nwl_cairo_state_data *statedata = nwl_state_get_sub(surface->state, &cairo_subimpl);
			if (!statedata) {
				statedata = calloc(1, sizeof(struct nwl_cairo_state_data));
				nwl_state_add_sub(surface->state, &cairo_subimpl, statedata);
			}
			if (!statedata->cairo_dev) {
				statedata->cairo_dev = cairo_egl_device_create(c->backend.egl->egl->display, c->backend.egl->context);
			}
			c->egl_surface = cairo_gl_surface_create_for_egl(statedata->cairo_dev, c->backend.egl->surface, scaled_width, scaled_height);
		}
		cairo_gl_surface_set_size(c->egl_surface, scaled_width, scaled_height);
	}
	surface->current_width = scaled_width;
	surface->current_height = scaled_height;
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
}

static void nwl_cairo_surface_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->egl_surface) {
		cairo_surface_destroy(c->egl_surface);
		c->egl_surface = NULL;
	}
	// Hack.. maybe it would be better to just get rid of surface_destroy?
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
}

static void nwl_cairo_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->shm) {
		nwl_shm_bufferman_destroy(c->backend.shm);
	} else {
		if (c->egl_surface) {
			cairo_surface_destroy(c->egl_surface);
		}
		nwl_egl_surface_destroy(c->backend.egl);
	}
	free(surface->render.data);
}

static void nwl_cairo_swap_buffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	wl_surface_damage_buffer(surface->wl.surface, 0, 0, surface->current_width, surface->current_height);
	if (wl_compositor_get_version(surface->state->wl.compositor) >= 5) {
		wl_surface_offset(surface->wl.surface, x, y);
		x = 0;
		y = 0;
	}
	if (c->shm) {
		if (c->next_buffer) {
			wl_surface_attach(surface->wl.surface, c->next_buffer->wl_buffer, x, y);
			wl_surface_commit(surface->wl.surface);
			c->next_buffer = NULL;
		}
	} else {
		cairo_gl_surface_swapbuffers(c->egl_surface);
	}
}

static void nwl_cairo_render(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	surface->render.rendering = true;
	if (c->shm) {
		if (!c->next_buffer) {
			c->next_buffer = nwl_shm_bufferman_get_next(c->backend.shm);
		}
		if (c->next_buffer) {
			c->renderfunc(surface, c->next_buffer->data);
		} else {
			// Didn't get a buffer. Increase buffer slots..
			nwl_shm_bufferman_set_slots(c->backend.shm, surface->state, c->backend.shm->num_slots + 1);
		}
	} else {
		c->renderfunc(surface, c->egl_surface);
	}
	surface->render.rendering = false;
}

static struct nwl_renderer_impl cairo_impl = {
	nwl_cairo_set_size,
	nwl_cairo_surface_destroy,
	nwl_cairo_swap_buffers,
	nwl_cairo_render,
	nwl_cairo_destroy
};

static void cairo_create_shm_buffer(struct nwl_shm_buffer *buf, struct nwl_shm_bufferman *bm) {
	buf->data = cairo_image_surface_create_for_data(buf->bufferdata, CAIRO_FORMAT_ARGB32, bm->width, bm->height, bm->stride);
}

static void cairo_destroy_shm_buffer(struct nwl_shm_buffer *buf, struct nwl_shm_bufferman *bm) {
	UNUSED(bm);
	cairo_surface_destroy(buf->data);
}

static struct nwl_shm_bufferman_renderer_impl cairo_shmbuffer_impl = {
	cairo_create_shm_buffer,
	cairo_destroy_shm_buffer
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
		struct nwl_egl_surface *egl_surface = nwl_egl_surface_create(surface->state);
		if (egl_surface) {
			if (!nwl_egl_surface_global_context(egl_surface)) {
				nwl_egl_surface_destroy(egl_surface);
			} else {
				dat->backend.egl = egl_surface;
				return;
			}
		}
	}
	dat->shm = true;
	dat->backend.shm = nwl_shm_bufferman_create();
	// Should slots default to 1 and then bufferman automagically bumps up the slots if needed?
	nwl_shm_bufferman_set_slots(dat->backend.shm, surface->state, 1);
	dat->backend.shm->impl = &cairo_shmbuffer_impl;
}
