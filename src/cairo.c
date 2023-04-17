#include <stdlib.h>
#include <stdio.h>
#include "nwl/cairo.h"
#include "nwl/nwl.h"
#include "nwl/shm.h"
#include "nwl/surface.h"

struct nwl_cairo_renderer_data {
	nwl_surface_cairo_render_t renderfunc;
	struct nwl_shm_bufferman shm;
	struct nwl_shm_buffer *next_buffer;
};

static void nwl_cairo_set_size(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	// If a buffer is queued up release it.
	// This should probably be more automagic..
	if (c->next_buffer) {
		nwl_shm_buffer_release(c->next_buffer);
		c->next_buffer = NULL;
	}
	nwl_shm_bufferman_resize(&c->shm, surface->state, scaled_width, scaled_height,
			cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, scaled_width), WL_SHM_FORMAT_ARGB8888);
	surface->current_width = scaled_width;
	surface->current_height = scaled_height;
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
}

static void nwl_cairo_surface_destroy(struct nwl_surface *surface) {
	// Hack.. maybe it would be better to just get rid of surface_destroy?
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
}

static void nwl_cairo_destroy(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	nwl_shm_bufferman_finish(&c->shm);
	free(surface->render.data);
	surface->render.impl = NULL;
}

static void nwl_cairo_swap_buffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	wl_surface_damage_buffer(surface->wl.surface, 0, 0, surface->current_width, surface->current_height);
	if ((x != 0 || y != 0) && wl_surface_get_version(surface->wl.surface) >= 5) {
		wl_surface_offset(surface->wl.surface, x, y);
		x = 0;
		y = 0;
	}
	if (c->next_buffer) {
		wl_surface_attach(surface->wl.surface, c->next_buffer->wl_buffer, x, y);
		wl_surface_commit(surface->wl.surface);
		c->next_buffer = NULL;
	}
}

static struct nwl_shm_buffer* get_next_buffer(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	struct nwl_shm_buffer *buffer = nwl_shm_bufferman_get_next(&c->shm);
	if (!buffer) {
		// Increase slots and try again..
		nwl_shm_bufferman_set_slots(&c->shm, surface->state, c->shm.num_slots + 1);
		buffer = nwl_shm_bufferman_get_next(&c->shm);
	}
	return buffer;
}

static void nwl_cairo_render(struct nwl_surface *surface) {
	struct nwl_cairo_renderer_data *c = surface->render.data;
	surface->render.rendering = true;
	if (!c->next_buffer) {
		c->next_buffer = get_next_buffer(surface);
	}
	if (c->next_buffer) {
		c->renderfunc(surface, c->next_buffer->data);
	}
	// Do something special if no buffer?
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
}
