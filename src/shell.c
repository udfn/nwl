#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

static void handle_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t width, uint32_t height) {
	UNUSED(layer);
	struct nwl_surface *surf = (struct nwl_surface*)data;
	if (surf->impl.configure) {
		surf->impl.configure(surf, width, height);
	} else if (surf->width != width || surf->height != height) {
		// refuse to set sizes to zero
		if (width > 0 && height > 0) {
			surf->width = width;
			surf->height = height;
			surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		}
	}
	surf->configure_serial = serial;
	surf->states = surf->states & ~NWL_SURFACE_STATE_NEEDS_CONFIGURE;
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
	UNUSED(xdg_surface);
	surf->configure_serial = serial;
	surf->states = surf->states & ~NWL_SURFACE_STATE_NEEDS_CONFIGURE;
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

static void handle_toplevel_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int width, int height) {
	UNUSED(xdg_toplevel);
	struct nwl_surface *surface = data;
	surface->role.toplevel.bounds_width = width;
	surface->role.toplevel.bounds_height = height;
}

static void handle_toplevel_wm_caps(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *caps) {
	UNUSED(xdg_toplevel);
	uint32_t *cap;
	unsigned char new_caps = 0;
	wl_array_for_each(cap, caps) {
		switch (*cap) {
			case XDG_TOPLEVEL_WM_CAPABILITIES_WINDOW_MENU:
				new_caps |= NWL_XDG_WM_CAP_WINDOW_MENU;break;
			case XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE:
				new_caps |= NWL_XDG_WM_CAP_MAXIMIZE;break;
			case XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN:
				new_caps |= NWL_XDG_WM_CAP_FULLSCREEN;break;
			case XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE:
				new_caps |= NWL_XDG_WM_CAP_MINIMIZE;break;
		}
	}
	struct nwl_surface *surface = data;
	surface->role.toplevel.wm_capabilities = new_caps;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close,
	handle_toplevel_bounds,
	handle_toplevel_wm_caps
};

static void handle_popup_configure(void *data, struct xdg_popup *xdg_popup, int32_t x, int32_t y, int32_t width, int32_t height) {
	UNUSED(xdg_popup);
	struct nwl_surface *surf = data;
	if (surf->width != (uint32_t)width || surf->height != (uint32_t)height) {
		surf->width = width;
		surf->height = height;
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	}
	surf->role.popup.lx = x;
	surf->role.popup.ly = y;
}

static void handle_popup_done(void *data, struct xdg_popup *xdg_popup) {
	UNUSED(xdg_popup);
	struct nwl_surface *surf = data;
	nwl_surface_destroy_later(surf);
}

static void handle_popup_repositioned(void *data, struct xdg_popup *xdg_popup, uint32_t token) {
	UNUSED(data);
	UNUSED(xdg_popup);
	struct nwl_surface *surf = data;
	surf->role.popup.reposition_token = token;
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
	surface->states |= NWL_SURFACE_STATE_NEEDS_CONFIGURE;
	return true;
}

bool nwl_surface_role_toplevel(struct nwl_surface *surface) {
	if (!surface->state->wl.xdg_wm_base || surface->role_id) {
		return false;
	}
	surface->wl.xdg_surface = xdg_wm_base_get_xdg_surface(surface->state->wl.xdg_wm_base, surface->wl.surface);
	surface->role.toplevel.wl = xdg_surface_get_toplevel(surface->wl.xdg_surface);
	surface->role.toplevel.wm_capabilities = 255;
	xdg_toplevel_add_listener(surface->role.toplevel.wl, &toplevel_listener, surface);
	xdg_surface_add_listener(surface->wl.xdg_surface, &surface_listener, surface);
	if (surface->title) {
		xdg_toplevel_set_title(surface->role.toplevel.wl, surface->title);
	}
	if (surface->state->wl.decoration) {
		surface->role.toplevel.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(surface->state->wl.decoration,
			surface->role.toplevel.wl);
		zxdg_toplevel_decoration_v1_add_listener(surface->role.toplevel.decoration, &decoration_listener, surface);
	}
	if (surface->state->xdg_app_id) {
		xdg_toplevel_set_app_id(surface->role.toplevel.wl, surface->state->xdg_app_id);
	}
	surface->role_id = NWL_SURFACE_ROLE_TOPLEVEL;
	surface->states |= NWL_SURFACE_STATE_CSD;
	surface->state->num_surfaces++;
	surface->states |= NWL_SURFACE_STATE_NEEDS_CONFIGURE;
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
	surface->states |= NWL_SURFACE_STATE_NEEDS_CONFIGURE;
	// Add to num_surfaces? How about to parent as a child?
	// Nah, for now that will just have to be manually managed :)
	return true;
}
