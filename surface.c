#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "viewporter.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

// Is in wayland.c
void surface_mark_dirty(struct nwl_surface *surface);

struct wl_callback_listener callback_listener;

static void nwl_surface_real_apply_size(struct nwl_surface *surface) {
	surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	surface->render.impl->apply_size(surface);
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
}

void nwl_surface_render(struct nwl_surface *surface) {
	surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_DRAW;
	if (surface->states & NWL_SURFACE_STATE_NEEDS_APPLY_SIZE) {
		nwl_surface_real_apply_size(surface);
	}
	surface->render.impl->render(surface);
}

static void cb_done(void *data, struct wl_callback *cb, uint32_t cb_data) {
	UNUSED(cb_data);
	struct nwl_surface *surf = data;
	surf->wl.frame_cb = NULL;
	wl_callback_destroy(cb);
	if (surf->states & NWL_SURFACE_STATE_NEEDS_DRAW) {
		nwl_surface_render(surf);
	}
}

struct wl_callback_listener callback_listener = {
	cb_done
};

static void surface_set_scale_from_outputs(struct nwl_surface *surf) {
	int scale = 1;
	struct nwl_surface_output *surfoutput;
	wl_list_for_each(surfoutput, &surf->outputs, link) {
		struct nwl_output *nwoutput = wl_output_get_user_data(surfoutput->output);
		if (nwoutput && nwoutput->scale > scale) {
			scale = nwoutput->scale;
		}
	}
	if (scale != surf->scale) {
		surf->scale = scale;
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		nwl_surface_set_need_draw(surf, true);
	}
}

static void handle_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	struct nwl_surface_output *surfoutput = calloc(1, sizeof(struct nwl_surface_output));
	surfoutput->output = output;
	wl_list_insert(&surf->outputs, &surfoutput->link);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}

static void handle_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	struct nwl_surface_output *surfoutput;
	wl_list_for_each(surfoutput, &surf->outputs, link) {
		if (surfoutput->output == output) {
			break;
		}
	}
	wl_list_remove(&surfoutput->link);
	free(surfoutput);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}
static const struct wl_surface_listener surface_listener = {
	handle_surface_enter,
	handle_surface_leave
};

struct nwl_surface *nwl_surface_create(struct nwl_state *state, char *title) {
	struct nwl_surface *newsurf = calloc(1, sizeof(struct nwl_surface));
	newsurf->state = state;
	newsurf->wl.surface = wl_compositor_create_surface(state->wl.compositor);
	newsurf->scale = 1;
	newsurf->title = title;
	wl_list_init(&newsurf->subsurfaces);
	wl_list_init(&newsurf->outputs);
	wl_list_init(&newsurf->dirtlink);
	wl_list_insert(&state->surfaces, &newsurf->link);
	wl_surface_set_user_data(newsurf->wl.surface, newsurf);
	wl_surface_add_listener(newsurf->wl.surface, &surface_listener, newsurf);
	return newsurf;
}

static void nwl_surface_destroy_role(struct nwl_surface *surface) {
	if (!surface->role_id) {
		return;
	}
	if (surface->wl.frame_cb) {
		wl_callback_destroy(surface->wl.frame_cb);
	}
	if (surface->wl.xdg_surface) {
		if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL) {
			if (surface->wl.xdg_decoration) {
				zxdg_toplevel_decoration_v1_destroy(surface->wl.xdg_decoration);
				surface->wl.xdg_decoration = NULL;
			}
			xdg_toplevel_destroy(surface->role.toplevel.wl);
		} else if (surface->role_id == NWL_SURFACE_ROLE_POPUP) {
			xdg_popup_destroy(surface->role.popup.wl);
		}
		xdg_surface_destroy(surface->wl.xdg_surface);
		surface->wl.xdg_surface = NULL;
	} else if (surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		zwlr_layer_surface_v1_destroy(surface->role.layer.wl);
	} else if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
		wl_subsurface_destroy(surface->role.subsurface.wl);
	}
	if (surface->wl.viewport) {
		wp_viewport_destroy(surface->wl.viewport);
	}
	if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL ||
			surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		surface->state->num_surfaces--;
	}
	wl_surface_destroy(surface->wl.surface);
}

void nwl_surface_destroy(struct nwl_surface *surface) {
	// clear any focuses
	nwl_seat_clear_focus(surface);
	if (!wl_list_empty(&surface->dirtlink)) {
		wl_list_remove(&surface->dirtlink);
	}
	wl_list_remove(&surface->link);
	if (surface->impl.destroy) {
		surface->impl.destroy(surface);
	}
	surface->render.impl->destroy(surface);
	struct nwl_surface_output *surfoutput, *surfoutputtmp;
	wl_list_for_each_safe(surfoutput, surfoutputtmp, &surface->outputs, link) {
		wl_list_remove(&surfoutput->link);
		free(surfoutput);
	}
	nwl_surface_destroy_role(surface);
	// And finally, destroy subsurfaces!
	struct nwl_surface *subsurf, *subsurftmp;
	wl_list_for_each_safe(subsurf, subsurftmp, &surface->subsurfaces, link) {
		nwl_surface_destroy(subsurf);
	}
	free(surface);
}

void nwl_surface_destroy_later(struct nwl_surface *surface) {
	if (surface->states & NWL_SURFACE_STATE_DESTROY) {
		return;
	}
	surface->states |= NWL_SURFACE_STATE_DESTROY;
	if (surface->wl.frame_cb) {
		wl_callback_destroy(surface->wl.frame_cb);
		surface->wl.frame_cb = NULL;
	}
	surface_mark_dirty(surface);
}

bool nwl_surface_set_vp_destination(struct nwl_surface *surface, int32_t width, int32_t height) {
	if (!surface->state->wl.viewporter) {
		return false;
	}
	if (!surface->wl.viewport) {
		surface->wl.viewport = wp_viewporter_get_viewport(surface->state->wl.viewporter, surface->wl.surface);
	}
	wp_viewport_set_destination(surface->wl.viewport, width, height);
	if (width == -1 && height == -1) {
		surface->actual_height = surface->height;
		surface->actual_width = surface->width;
	} else {
		surface->actual_height = height;
		surface->actual_width = width;
	}
	return true;
}

void nwl_surface_set_size(struct nwl_surface *surface, uint32_t width, uint32_t height) {
	surface->desired_height = height;
	surface->desired_width = width;
	switch (surface->role_id) {
		case NWL_SURFACE_ROLE_LAYER:
			zwlr_layer_surface_v1_set_size(surface->role.layer.wl, width, height);
			return;
		case NWL_SURFACE_ROLE_TOPLEVEL:
		case NWL_SURFACE_ROLE_POPUP:
		case NWL_SURFACE_ROLE_NONE:
			return;
		default:
			surface->width = width;
			surface->height = height;
			surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
			return;
	}
}

void nwl_surface_swapbuffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	surface->frame++;
	if (!surface->wl.frame_cb) {
		surface->wl.frame_cb = wl_surface_frame(surface->wl.surface);
		wl_callback_add_listener(surface->wl.frame_cb, &callback_listener, surface);
	}
	uint32_t scaled_width, scaled_height;
	scaled_width = surface->width*surface->scale;
	scaled_height = surface->height*surface->scale;
	wl_surface_damage_buffer(surface->wl.surface, 0, 0, scaled_width, scaled_height);
	surface->render.impl->swap_buffers(surface, x, y);
}

void nwl_surface_set_need_draw(struct nwl_surface *surface, bool render) {
	surface->states |= NWL_SURFACE_STATE_NEEDS_DRAW;
	if (surface->wl.frame_cb || surface->render.rendering || surface->needs_configure) {
		return;
	}
	if (render) {
		nwl_surface_render(surface);
		return;
	}
	surface_mark_dirty(surface);
}

void nwl_surface_role_unset(struct nwl_surface *surface) {
	if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
		wl_list_remove(&surface->link);
		wl_list_insert(&surface->state->surfaces, &surface->link);
		surface->parent = NULL;
	}
	nwl_surface_destroy_role(surface);
	memset(&surface->wl, 0, sizeof(surface->wl));
	surface->wl.surface = wl_compositor_create_surface(surface->state->wl.compositor);
	wl_surface_set_user_data(surface->wl.surface, surface);
	wl_surface_add_listener(surface->wl.surface, &surface_listener, surface);
	surface->role_id = 0;
	surface->render.impl->surface_destroy(surface);
	if (surface->states & NWL_SURFACE_STATE_NEEDS_DRAW) {
		surface->states = (surface->states & ~NWL_SURFACE_STATE_NEEDS_DRAW);
	}
	// Should nwl remember subsurface state and restore it?
	struct nwl_surface *sub;
	wl_list_for_each(sub, &surface->subsurfaces, link) {
		wl_subsurface_destroy(sub->role.subsurface.wl);
		sub->role.subsurface.wl = wl_subcompositor_get_subsurface(surface->state->wl.subcompositor, sub->wl.surface, surface->wl.surface);
	}
}

bool nwl_surface_role_subsurface(struct nwl_surface *surface, struct nwl_surface *parent) {
	if (surface->role_id) {
		return false;
	}
	surface->role.subsurface.wl = wl_subcompositor_get_subsurface(surface->state->wl.subcompositor,
			surface->wl.surface, parent->wl.surface);
	surface->parent = parent;
	wl_list_remove(&surface->link);
	wl_list_insert(&parent->subsurfaces, &surface->link);
	surface->role_id = NWL_SURFACE_ROLE_SUB;
	return true;
}
