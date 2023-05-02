#include <stdlib.h>
#include <stdio.h>
#include <cairo.h>
#include "nwl/cairo.h"
#include "nwl/nwl.h"
#include "nwl/shm.h"
#include "nwl/surface.h"

struct nwl_cairo_renderer_data {
	nwl_surface_cairo_render_t renderfunc;
	struct nwl_shm_bufferman shm;
	struct nwl_cairo_surface cairo_surfaces[NWL_SHM_BUFFERMAN_MAX_BUFFERS];
	int next_buffer;
	int prev_buffer;
	bool damage_tracking;
};

static void nwl_cairo_set_size(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	// If a buffer is queued up release it.
	// This should probably be more automagic..
	if (c->next_buffer != -1) {
		nwl_shm_buffer_release(&c->shm.buffers[c->next_buffer]);
		c->next_buffer = -1;
	}
	c->prev_buffer = -1;
	nwl_shm_bufferman_resize(&c->shm, surface->state, scaled_width, scaled_height,
			cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, scaled_width), WL_SHM_FORMAT_ARGB8888);
	surface->current_width = scaled_width;
	surface->current_height = scaled_height;
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
}

static void nwl_cairo_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	nwl_shm_bufferman_finish(&c->shm);
	free(surface->render.data);
	surface->render.impl = NULL;
}

static void nwl_cairo_swap_buffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	if (c->next_buffer == -1) {
		return;
	}
	c->prev_buffer = c->next_buffer;
	if (!c->damage_tracking) {
		wl_surface_damage_buffer(surface->wl.surface, 0, 0, surface->current_width, surface->current_height);
	}
	c->cairo_surfaces[c->next_buffer].rerender = false;
	if ((x != 0 || y != 0) && wl_surface_get_version(surface->wl.surface) >= 5) {
		wl_surface_offset(surface->wl.surface, x, y);
		x = 0;
		y = 0;
	}
	wl_surface_attach(surface->wl.surface, c->shm.buffers[c->next_buffer].wl_buffer, x, y);
	wl_surface_commit(surface->wl.surface);
	c->next_buffer = -1;
}

static int get_next_buffer(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	int buffer = nwl_shm_bufferman_get_next(&c->shm);
	if (buffer == -1) {
		// Increase slots and try again..
		nwl_shm_bufferman_set_slots(&c->shm, surface->state, c->shm.num_slots + 1);
		buffer = nwl_shm_bufferman_get_next(&c->shm);
		c->prev_buffer = -1;
	}
	return buffer;
}

static void nwl_cairo_render(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	surface->render.rendering = true;
	if (c->next_buffer == -1) {
		c->next_buffer = get_next_buffer(surface);
	}
	if (c->next_buffer != -1) {
		// Only do this blit if rendering to a different buffer, to take advantage of
		// compositors that immediately release buffers.
		if (c->damage_tracking && c->prev_buffer != -1 && c->prev_buffer != c->next_buffer) {
			struct nwl_cairo_surface *csurf = &c->cairo_surfaces[c->next_buffer];
			struct nwl_cairo_surface *prevsurf = &c->cairo_surfaces[c->prev_buffer];
			cairo_save(csurf->ctx);
			cairo_reset_clip(csurf->ctx);
			cairo_identity_matrix(csurf->ctx);
			cairo_set_source_surface(csurf->ctx, prevsurf->surface, 0, 0);
			cairo_set_operator(csurf->ctx, CAIRO_OPERATOR_SOURCE);
			cairo_paint(csurf->ctx);
			cairo_restore(csurf->ctx);
		} else if (c->prev_buffer == -1) {
			c->cairo_surfaces[c->next_buffer].rerender = true;
		}
		c->renderfunc(surface, &c->cairo_surfaces[c->next_buffer]);
	}
	// Do something special if no buffer?
	surface->render.rendering = false;
}

static struct nwl_renderer_impl cairo_impl = {
	nwl_cairo_set_size,
	nwl_cairo_swap_buffers,
	nwl_cairo_render,
	nwl_cairo_destroy
};

static void cairo_create_shm_buffer(unsigned int buf_idx, struct nwl_shm_bufferman *bm) {
	struct nwl_cairo_renderer_data *data = wl_container_of(bm, data, shm);
	data->cairo_surfaces[buf_idx].surface = cairo_image_surface_create_for_data(
		bm->buffers[buf_idx].bufferdata, CAIRO_FORMAT_ARGB32, bm->width, bm->height, bm->stride);
	cairo_t *ctx = cairo_create(data->cairo_surfaces[buf_idx].surface);
	data->cairo_surfaces[buf_idx].ctx = ctx;
}

static void cairo_destroy_shm_buffer(unsigned int buf_idx, struct nwl_shm_bufferman *bm) {
	struct nwl_cairo_renderer_data *data = wl_container_of(bm, data, shm);
	cairo_destroy(data->cairo_surfaces[buf_idx].ctx);
	cairo_surface_destroy(data->cairo_surfaces[buf_idx].surface);
}

static struct nwl_shm_bufferman_renderer_impl cairo_shmbuffer_impl = {
	cairo_create_shm_buffer,
	cairo_destroy_shm_buffer
};

void nwl_surface_renderer_cairo(struct nwl_surface *surface, nwl_surface_cairo_render_t renderfunc, int flags) {
	if (surface->render.impl) {
		surface->render.impl->destroy(surface);
	}
	surface->render.impl = &cairo_impl;
	surface->render.data = calloc(1, sizeof(struct nwl_cairo_renderer_data));
	struct nwl_cairo_renderer_data *dat = surface->render.data;
	dat->renderfunc = renderfunc;
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	nwl_shm_bufferman_init(&dat->shm);
	dat->shm.impl = &cairo_shmbuffer_impl;
	dat->prev_buffer = -1;
	if (flags & NWL_CAIRO_DAMAGE_TRACKING) {
		dat->damage_tracking = true;
	}
}
