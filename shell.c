#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "viewporter.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

static void handle_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t width, uint32_t height ) {
	struct nwl_surface *surf = (struct nwl_surface*)data;
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	if (surf->impl.configure) {
		surf->impl.configure(surf, width, height);
	} else if (surf->width != width || surf->height != height) {
		surf->width = width;
		surf->height = height;
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	}
	surf->needs_configure = false;
	nwl_surface_set_need_draw(surf, true);
}

static void handle_layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	UNUSED(layer);
	struct nwl_surface *surf = data;
	nwl_surface_destroy_later(surf);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	handle_layer_configure,
	handle_layer_closed
};

static void handle_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct nwl_surface *surf = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	// Ugly hack!
	xdg_surface_set_window_geometry(xdg_surface, 0, 0, surf->width, surf->height);
	surf->needs_configure = false;
	nwl_surface_set_need_draw(surf, true);
}

static const struct xdg_surface_listener surface_listener = {
	handle_surface_configure
};

static void handle_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	UNUSED(xdg_toplevel);
	struct nwl_surface *surf = data;
	uint32_t *state = 0;
	enum nwl_surface_states newstates = 0;
	wl_array_for_each(state, states) {
		switch (*state) {
			case XDG_TOPLEVEL_STATE_MAXIMIZED:
				newstates |= NWL_SURFACE_STATE_MAXIMIZED;
				break;
			case XDG_TOPLEVEL_STATE_ACTIVATED:
				newstates |= NWL_SURFACE_STATE_ACTIVE;
				break;
			case XDG_TOPLEVEL_STATE_RESIZING:
				newstates |= NWL_SURFACE_STATE_RESIZING;
				break;
			case XDG_TOPLEVEL_STATE_FULLSCREEN:
				newstates |= NWL_SURFACE_STATE_FULLSCREEN;
				break;
			case XDG_TOPLEVEL_STATE_TILED_LEFT:
				newstates |= NWL_SURFACE_STATE_TILE_LEFT;
				break;
			case XDG_TOPLEVEL_STATE_TILED_RIGHT:
				newstates |= NWL_SURFACE_STATE_TILE_RIGHT;
				break;
			case XDG_TOPLEVEL_STATE_TILED_TOP:
				newstates |= NWL_SURFACE_STATE_TILE_TOP;
				break;
			case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
				newstates |= NWL_SURFACE_STATE_TILE_BOTTOM;
				break;
		}
	}
	surf->states = (surf->states & ~0xFF) | newstates;
	if (surf->impl.configure) {
		surf->impl.configure(surf, width, height);
		return;
	}
	if (width == 0) {
		width = surf->desired_width;
	}
	if (height == 0) {
		height = surf->desired_height;
	}
	if ((uint32_t)width != surf->width || (uint32_t)height != surf->height) {
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	}
	surf->width = width;
	surf->height = height;
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	UNUSED(xdg_toplevel);
	struct nwl_surface *surf = data;
	if (surf->impl.close) {
		surf->impl.close(surf);
		return;
	}
	nwl_surface_destroy_later(surf);
}

static const struct xdg_toplevel_listener toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close
};

static void handle_popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x, int32_t y, int32_t width, int32_t height) {
	UNUSED(xdg_popup);
	struct nwl_surface *surf = data;
	surf->width = width;
	surf->height = height;
	surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	UNUSED(x);
	UNUSED(y);
}

static void handle_popup_done(void *data, struct xdg_popup *xdg_popup) {
	UNUSED(xdg_popup);
	struct nwl_surface *surf = data;
	nwl_surface_destroy_later(surf);
}

static void handle_popup_repositioned(void *data, struct xdg_popup *xdg_popup, uint32_t token) {
	UNUSED(data);
	UNUSED(xdg_popup);
	UNUSED(token);
	// whatever
}

static const struct xdg_popup_listener popup_listener = {
	handle_popup_configure,
	handle_popup_done,
	handle_popup_repositioned
};

static void handle_decoration_configure(
		void *data,
		struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration_v1,
		uint32_t mode) {
	UNUSED(zxdg_toplevel_decoration_v1);
	struct nwl_surface *surf = (struct nwl_surface*)data;
	if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
		surf->states |= NWL_SURFACE_STATE_CSD;
	} else {
		surf->states = surf->states & ~NWL_SURFACE_STATE_CSD;
	}
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	handle_decoration_configure
};

bool nwl_surface_role_layershell(struct nwl_surface *surface, struct wl_output *output, uint32_t layer) {
	if (!surface->state->wl.layer_shell || surface->role_id) {
		return false;
	}
	surface->role.layer.wl = zwlr_layer_shell_v1_get_layer_surface(surface->state->wl.layer_shell,
			surface->wl.surface, output, layer, surface->title);
	zwlr_layer_surface_v1_add_listener(surface->role.layer.wl, &layer_listener, surface);
	surface->role_id = NWL_SURFACE_ROLE_LAYER;
	surface->state->num_surfaces++;
	surface->needs_configure = true;
	return true;
}

bool nwl_surface_role_toplevel(struct nwl_surface *surface) {
	if (!surface->state->wl.xdg_wm_base || surface->role_id) {
		return false;
	}
	surface->wl.xdg_surface = xdg_wm_base_get_xdg_surface(surface->state->wl.xdg_wm_base, surface->wl.surface);
	surface->role.toplevel.wl = xdg_surface_get_toplevel(surface->wl.xdg_surface);
	xdg_toplevel_add_listener(surface->role.toplevel.wl, &toplevel_listener, surface);
	xdg_surface_add_listener(surface->wl.xdg_surface, &surface_listener, surface);
	xdg_toplevel_set_title(surface->role.toplevel.wl, surface->title);
	if (surface->state->wl.decoration) {
		surface->wl.xdg_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(surface->state->wl.decoration, surface->role.toplevel.wl);
		zxdg_toplevel_decoration_v1_add_listener(surface->wl.xdg_decoration, &decoration_listener, surface);
	}
	if (surface->state->xdg_app_id) {
		xdg_toplevel_set_app_id(surface->role.toplevel.wl, surface->state->xdg_app_id);
	}
	surface->role_id = NWL_SURFACE_ROLE_TOPLEVEL;
	surface->states |= NWL_SURFACE_STATE_CSD;
	surface->state->num_surfaces++;
	surface->needs_configure = true;
	return true;
}

bool nwl_surface_role_popup(struct nwl_surface *surface, struct nwl_surface *parent, struct xdg_positioner *positioner) {
	if (!surface->state->wl.xdg_wm_base || surface->role_id || (parent != NULL &&
			!parent->wl.xdg_surface && parent->role_id != NWL_SURFACE_ROLE_LAYER)) {
		return false;
	}
	surface->wl.xdg_surface = xdg_wm_base_get_xdg_surface(surface->state->wl.xdg_wm_base, surface->wl.surface);
	struct xdg_surface *xdg_parent = parent ? parent->wl.xdg_surface : NULL;
	surface->role.popup.wl = xdg_surface_get_popup(surface->wl.xdg_surface, xdg_parent, positioner);
	if (parent && !xdg_parent) {
		zwlr_layer_surface_v1_get_popup(parent->role.layer.wl, surface->role.popup.wl);
	}
	xdg_surface_add_listener(surface->wl.xdg_surface, &surface_listener, surface);
	xdg_popup_add_listener(surface->role.popup.wl, &popup_listener, surface);
	surface->role_id = NWL_SURFACE_ROLE_POPUP;
	surface->needs_configure = true;
	// Add to num_surfaces? How about to parent as a child?
	// Nah, for now that will just have to be manually managed :)
	return true;
}
