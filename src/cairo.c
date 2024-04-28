#include <cairo.h>
#include <wayland-client-protocol.h>
#include "nwl/cairo.h"
#include "nwl/shm.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

static void nwl_cairo_renderer_set_size(struct nwl_cairo_renderer *renderer, struct wl_shm *wl_shm, uint32_t width, uint32_t height) {
	if (renderer->next_buffer != -1) {
		renderer->next_buffer = -1;
	}
	renderer->prev_buffer = -1;
	nwl_shm_bufferman_resize(&renderer->shm, wl_shm, width, height,
			cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width), WL_SHM_FORMAT_ARGB8888);
}


void nwl_cairo_renderer_submit(struct nwl_cairo_renderer *renderer, struct nwl_surface *surface, int32_t x, int32_t y) {
	if (renderer->next_buffer == -1) {
		return;
	}
	renderer->prev_buffer = renderer->next_buffer;
	renderer->cairo_surfaces[renderer->next_buffer].rerender = false;
	if ((x != 0 || y != 0) && wl_surface_get_version(surface->wl.surface) >= 5) {
		wl_surface_offset(surface->wl.surface, x, y);
		x = 0;
		y = 0;
	}
	renderer->shm.buffers[renderer->next_buffer].flags |= NWL_SHM_BUFFER_ACQUIRED;
	wl_surface_attach(surface->wl.surface, renderer->shm.buffers[renderer->next_buffer].wl_buffer, x, y);
	renderer->next_buffer = -1;
	nwl_surface_buffer_submitted(surface);
	wl_surface_commit(surface->wl.surface);
}

static int get_next_buffer(struct nwl_cairo_renderer *renderer, struct wl_shm *wl_shm) {
	int buffer = nwl_shm_bufferman_get_next(&renderer->shm);
	if (buffer == -1) {
		// Increase slots and try again..
		nwl_shm_bufferman_set_slots(&renderer->shm, wl_shm, renderer->shm.num_slots + 1);
		buffer = nwl_shm_bufferman_get_next(&renderer->shm);
		renderer->prev_buffer = -1;
	}
	return buffer;
}

struct nwl_cairo_surface *nwl_cairo_renderer_get_surface(struct nwl_cairo_renderer *renderer, struct nwl_surface *surface, bool copyprevious) {
	if (surface->states & NWL_SURFACE_STATE_NEEDS_APPLY_SIZE) {
		surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		uint32_t scaled_width = surface->width * surface->scale;
		uint32_t scaled_height = surface->height * surface->scale;
		nwl_cairo_renderer_set_size(renderer, surface->state->wl.shm, scaled_width, scaled_height);
		surface->current_width = scaled_width;
		surface->current_height = scaled_height;
		wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
	}
	if (renderer->next_buffer == -1) {
		renderer->next_buffer = get_next_buffer(renderer, surface->state->wl.shm);
		if (renderer->next_buffer != -1) {
			// Only do this blit if rendering to a different buffer, to take advantage of
			// compositors that immediately release buffers.
			if (copyprevious && renderer->prev_buffer != -1 && renderer->prev_buffer != renderer->next_buffer) {
				struct nwl_cairo_surface *csurf = &renderer->cairo_surfaces[renderer->next_buffer];
				struct nwl_cairo_surface *prevsurf = &renderer->cairo_surfaces[renderer->prev_buffer];
				cairo_save(csurf->ctx);
				cairo_reset_clip(csurf->ctx);
				cairo_identity_matrix(csurf->ctx);
				cairo_set_source_surface(csurf->ctx, prevsurf->surface, 0, 0);
				cairo_set_operator(csurf->ctx, CAIRO_OPERATOR_SOURCE);
				cairo_paint(csurf->ctx);
				cairo_restore(csurf->ctx);
			} else {
				wl_surface_damage_buffer(surface->wl.surface, 0, 0, surface->current_width, surface->current_height);
				renderer->cairo_surfaces[renderer->next_buffer].rerender = true;
			}
		}
	}
	return renderer->next_buffer != -1 ? &renderer->cairo_surfaces[renderer->next_buffer] : NULL;
}

static void cairo_create_shm_buffer(unsigned int buf_idx, struct nwl_shm_bufferman *bm) {
	struct nwl_cairo_renderer *data = wl_container_of(bm, data, shm);
	data->cairo_surfaces[buf_idx].surface = cairo_image_surface_create_for_data(
		bm->buffers[buf_idx].bufferdata, CAIRO_FORMAT_ARGB32, bm->width, bm->height, bm->stride);
	cairo_t *ctx = cairo_create(data->cairo_surfaces[buf_idx].surface);
	data->cairo_surfaces[buf_idx].ctx = ctx;
}

static void cairo_destroy_shm_buffer(unsigned int buf_idx, struct nwl_shm_bufferman *bm) {
	struct nwl_cairo_renderer *data = wl_container_of(bm, data, shm);
	cairo_destroy(data->cairo_surfaces[buf_idx].ctx);
	cairo_surface_destroy(data->cairo_surfaces[buf_idx].surface);
}

static struct nwl_shm_bufferman_renderer_impl cairo_shmbuffer_impl = {
	cairo_create_shm_buffer,
	cairo_destroy_shm_buffer
};

void nwl_cairo_renderer_init(struct nwl_cairo_renderer *renderer) {
	nwl_shm_bufferman_init(&renderer->shm);
	renderer->shm.impl = &cairo_shmbuffer_impl;
	renderer->prev_buffer = -1;
	renderer->next_buffer = -1;
}

void nwl_cairo_renderer_finish(struct nwl_cairo_renderer *renderer) {
	nwl_shm_bufferman_finish(&renderer->shm);
}
