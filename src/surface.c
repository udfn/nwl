#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wayland-client-core.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "nwl/nwl.h"
#include "nwl/config.h"
#include "nwl/surface.h"
#if NWL_HAS_SEAT
#include "nwl/seat.h"
#endif

// Is in wayland.c
void surface_mark_dirty(struct nwl_surface *surface);

struct wl_callback_listener callback_listener;

void nwl_surface_update(struct nwl_surface *surface) {
	surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_UPDATE;
	surface->impl.update(surface);
}

static void cb_done(void *data, struct wl_callback *cb, uint32_t cb_data) {
	UNUSED(cb_data);
	struct nwl_surface *surf = data;
	surf->wl.frame_cb = NULL;
	wl_callback_destroy(cb);
	if (surf->states & NWL_SURFACE_STATE_NEEDS_UPDATE) {
		nwl_surface_update(surf);
	}
}

struct wl_callback_listener callback_listener = {
	cb_done
};

static void surface_autoscale(struct nwl_surface *surf) {
	// Prefer preferred scale, if set
	if (surf->scale_preferred) {
		if (surf->scale != surf->scale_preferred) {
			surf->scale = surf->scale_preferred;
			nwl_surface_set_need_update(surf, true);
		}
		return;
	}
	if (surf->outputs.amount == 0) {
		return;
	}
	int scale = 1;
	for (unsigned int i = 0; i < surf->outputs.amount; i++) {
		struct nwl_output *output = surf->outputs.outputs[i];
		if (output->scale > scale) {
			scale = output->scale;
		}
	}
	if (scale != surf->scale) {
		surf->scale = scale;
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		nwl_surface_set_need_update(surf, true);
		// Compositor might not send output events to subsurfaces..
		struct nwl_surface *sub;
		wl_list_for_each(sub, &surf->subsurfaces, link) {
			if (!(sub->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE) && sub->scale != scale) {
				sub->scale = scale;
				sub->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
				nwl_surface_set_need_update(sub, true);
			}
		}
	}
}

static void handle_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	surf->outputs.outputs = realloc(surf->outputs.outputs, sizeof(struct nwl_output*) * ++surf->outputs.amount);
	surf->outputs.outputs[surf->outputs.amount-1] = wl_output_get_user_data(output);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_autoscale(surf);
	}
}

static void handle_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	if (surf->outputs.amount == 1) {
		// Left all outputs, just free the array and don't do anything clever
		free(surf->outputs.outputs);
		surf->outputs.outputs = NULL;
		surf->outputs.amount = 0;
		return;
	}
	struct nwl_output *newoutputs[surf->outputs.amount-1];
	int new_i = 0;
	for (unsigned int i = 0; i < surf->outputs.amount; i++) {
		struct nwl_output *nwloutput = surf->outputs.outputs[i];
		if (nwloutput->output != output) {
			newoutputs[new_i++] = nwloutput;
		}
	}
	surf->outputs.outputs = realloc(surf->outputs.outputs, sizeof(struct nwl_output*) * --surf->outputs.amount);
	memcpy(surf->outputs.outputs, newoutputs, sizeof(struct nwl_output*) * surf->outputs.amount);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_autoscale(surf);
	}
}

static void handle_preferred_scale(void *data, struct wl_surface *surface, int32_t factor) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	surf->scale_preferred = factor;
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_autoscale(surf);
	}
}

static void handle_preferred_transform(void *data, struct wl_surface *surface, uint32_t transform) {
	// don't care, yet..?
	UNUSED(data);
	UNUSED(surface);
	UNUSED(transform);
}

static const struct wl_surface_listener surface_listener = {
	handle_surface_enter,
	handle_surface_leave,
	handle_preferred_scale,
	handle_preferred_transform
};

void nwl_surface_init(struct nwl_surface *surface, struct nwl_core *core, const char *title) {
	surface->core = core;
	surface->frame = 0;
	surface->defer_update = false;
	surface->wl.surface = NULL;
	surface->wl.xdg_surface = NULL;
	surface->wl.frame_cb = NULL;
	surface->outputs.amount = 0;
	surface->outputs.outputs = NULL;
	surface->role_id = NWL_SURFACE_ROLE_NONE;
	surface->wl.surface = wl_compositor_create_surface(core->wl.compositor);
	surface->scale = 1;
	surface->scale_preferred = 0;
	if (surface->desired_height == 0) {
		surface->desired_height = 480;
	}
	if (surface->desired_width == 0) {
		surface->desired_width = 640;
	}
	surface->configure_serial = 0;
	surface->states = 0;
	surface->width = 0;
	surface->height = 0;
	if (title) {
		surface->title = strdup(title);
	} else {
		surface->title = NULL;
	}
	wl_list_init(&surface->subsurfaces);
	wl_list_init(&surface->dirtlink);
	wl_list_insert(&core->surfaces, &surface->link);
	wl_surface_set_user_data(surface->wl.surface, surface);
	wl_surface_add_listener(surface->wl.surface, &surface_listener, surface);
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
			if (surface->role.toplevel.decoration) {
				zxdg_toplevel_decoration_v1_destroy(surface->role.toplevel.decoration);
				surface->role.toplevel.decoration = NULL;
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
	if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL ||
			surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		surface->core->num_surfaces--;
	}
	wl_surface_destroy(surface->wl.surface);
}

void nwl_surface_destroy(struct nwl_surface *surface) {
#if NWL_HAS_SEAT
	// clear any focuses
	nwl_seat_clear_focus(surface);
#endif
	if (!wl_list_empty(&surface->dirtlink)) {
		wl_list_remove(&surface->dirtlink);
	}
	wl_list_remove(&surface->link);

	if (surface->outputs.outputs) {
		free(surface->outputs.outputs);
	}
	nwl_surface_destroy_role(surface);
	// And finally, destroy subsurfaces!
	struct nwl_surface *subsurf, *subsurftmp;
	wl_list_for_each_safe(subsurf, subsurftmp, &surface->subsurfaces, link) {
		nwl_surface_destroy(subsurf);
	}
	if (surface->title) {
		free(surface->title);
	}
	if (surface->impl.destroy) {
		surface->impl.destroy(surface);
	}
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

void nwl_surface_set_title(struct nwl_surface *surface, const char *title) {
	if (surface->title) {
		free(surface->title);
	}
	if (title) {
		surface->title = strdup(title);
		if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL) {
			xdg_toplevel_set_title(surface->role.toplevel.wl, title);
		}
	} else {
		surface->title = NULL;
	}
}

static void nwl_surface_ack_configure(struct nwl_surface *surface) {
	switch (surface->role_id) {
		case NWL_SURFACE_ROLE_LAYER:
			zwlr_layer_surface_v1_ack_configure(surface->role.layer.wl, surface->configure_serial);
			break;
		case NWL_SURFACE_ROLE_TOPLEVEL:
		case NWL_SURFACE_ROLE_POPUP:
			xdg_surface_ack_configure(surface->wl.xdg_surface, surface->configure_serial);
			break;
	}
	surface->configure_serial = 0;
}

void nwl_surface_request_callback(struct nwl_surface *surface) {
	if (!surface->wl.frame_cb) {
		surface->wl.frame_cb = wl_surface_frame(surface->wl.surface);
		wl_callback_add_listener(surface->wl.frame_cb, &callback_listener, surface);
	}
}

void nwl_surface_buffer_submitted(struct nwl_surface *surface) {
	surface->frame++;
	nwl_surface_request_callback(surface);
	if (surface->configure_serial) {
		nwl_surface_ack_configure(surface);
	}
}

void nwl_surface_set_need_update(struct nwl_surface *surface, bool now) {
	surface->states |= NWL_SURFACE_STATE_NEEDS_UPDATE;
	if (surface->wl.frame_cb || surface->defer_update || surface->states & NWL_SURFACE_STATE_NEEDS_CONFIGURE) {
		return;
	}
	if (now) {
		nwl_surface_update(surface);
		return;
	}
	surface_mark_dirty(surface);
}

void nwl_surface_role_unset(struct nwl_surface *surface) {
	if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
		wl_list_remove(&surface->link);
		wl_list_insert(&surface->core->surfaces, &surface->link);
	}
	nwl_surface_destroy_role(surface);
	surface->states = 0;
	memset(&surface->wl, 0, sizeof(surface->wl));
	surface->wl.surface = wl_compositor_create_surface(surface->core->wl.compositor);
	wl_surface_set_user_data(surface->wl.surface, surface);
	wl_surface_add_listener(surface->wl.surface, &surface_listener, surface);
	surface->role_id = 0;
	// Should nwl remember subsurface state and restore it?
	struct nwl_surface *sub;
	wl_list_for_each(sub, &surface->subsurfaces, link) {
		wl_subsurface_destroy(sub->role.subsurface.wl);
		sub->role.subsurface.wl = wl_subcompositor_get_subsurface(surface->core->wl.subcompositor, sub->wl.surface, surface->wl.surface);
	}
}

bool nwl_surface_role_subsurface(struct nwl_surface *surface, struct nwl_surface *parent) {
	if (surface->role_id) {
		return false;
	}
	surface->role.subsurface.wl = wl_subcompositor_get_subsurface(surface->core->wl.subcompositor,
			surface->wl.surface, parent->wl.surface);
	surface->role.subsurface.parent = parent;
	wl_list_remove(&surface->link);
	wl_list_insert(&parent->subsurfaces, &surface->link);
	surface->role_id = NWL_SURFACE_ROLE_SUB;
	return true;
}
