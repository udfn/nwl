#ifndef _NWL_SURFACE_H
#define _NWL_SURFACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-client.h>
#include "seat.h"

struct xdg_positioner;

enum nwl_surface_flags {
	NWL_SURFACE_FLAG_NO_AUTOSCALE = 1 << 0,
	NWL_SURFACE_FLAG_NO_AUTOCURSOR = 1 << 1, // ugh, this one shouldn't stay!
};

// This is basically the xdg toplevel states + nwl nonsense..
enum nwl_surface_states {
	NWL_SURFACE_STATE_ACTIVE = 1 << 0,
	NWL_SURFACE_STATE_MAXIMIZED = 1 << 1,
	NWL_SURFACE_STATE_FULLSCREEN = 1 << 2,
	NWL_SURFACE_STATE_RESIZING = 1 << 3,
	NWL_SURFACE_STATE_TILE_LEFT = 1 << 4,
	NWL_SURFACE_STATE_TILE_RIGHT = 1 << 5,
	NWL_SURFACE_STATE_TILE_TOP = 1 << 6,
	NWL_SURFACE_STATE_TILE_BOTTOM = 1 << 7,

	NWL_SURFACE_STATE_CSD = 1 << 8,
	NWL_SURFACE_STATE_NEEDS_DRAW = 1 << 9,
	NWL_SURFACE_STATE_NEEDS_APPLY_SIZE = 1 << 10,
	NWL_SURFACE_STATE_DESTROY = 1 << 11,
	NWL_SURFACE_STATE_NEEDS_CONFIGURE = 1 << 12
};

enum nwl_surface_role {
	NWL_SURFACE_ROLE_NONE = 0,
	NWL_SURFACE_ROLE_TOPLEVEL,
	NWL_SURFACE_ROLE_POPUP,
	NWL_SURFACE_ROLE_LAYER,
	NWL_SURFACE_ROLE_SUB,
	NWL_SURFACE_ROLE_CURSOR,
	NWL_SURFACE_ROLE_DRAGICON
};

struct nwl_surface;
typedef void (*nwl_surface_destroy_t)(struct nwl_surface *surface);
typedef void (*nwl_surface_configure_t)(struct nwl_surface *surface, uint32_t width, uint32_t height);
typedef void (*nwl_surface_input_pointer_t)(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_pointer_event *event);
typedef void (*nwl_surface_input_keyboard_t)(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_keyboard_event *event);

typedef void (*nwl_surface_generic_func_t)(struct nwl_surface *surface);

struct nwl_renderer_impl {
	nwl_surface_generic_func_t apply_size;
	nwl_surface_generic_func_t surface_destroy;
	void (*swap_buffers)(struct nwl_surface *surface, int32_t x, int32_t y);
	nwl_surface_generic_func_t render;
	nwl_surface_generic_func_t destroy;
};

// Cairo.. or whatever you want, really!
struct nwl_renderer {
	struct nwl_renderer_impl *impl;
	void *data;
	bool rendering;
};

struct nwl_surface_output {
	struct wl_list link;
	// Should this be nwl_output instead?
	struct wl_output *output;
};

struct nwl_surface {
	struct wl_list link; // either linked to nwl_state, or another nwl_surface if subsurface
	struct wl_list dirtlink; // link if dirty
	struct nwl_state *state;
	struct {
		struct wl_surface *surface;
		struct xdg_surface *xdg_surface;
		struct zxdg_toplevel_decoration_v1 *xdg_decoration;
		struct wp_viewport *viewport;
		struct wl_callback *frame_cb;
	} wl;
	struct nwl_renderer render;
	uint32_t width, height;
	uint32_t desired_width, desired_height;
	uint32_t current_width, current_height; // unlike the others, this one is scaled!
	uint32_t configure_serial;
	int scale;
	struct nwl_surface *parent; // if a subsurface
	struct wl_list outputs; // nwl_surface_output
	struct wl_list subsurfaces; // nwl_surface
	enum nwl_surface_flags flags;
	enum nwl_surface_states states;
	char *title;
	char role_id; // nwl_surface_role, if it has one.
	union {
		struct {
			struct xdg_toplevel *wl;
		} toplevel;
		struct {
			struct wl_subsurface *wl;
		} subsurface;
		struct {
			struct xdg_popup *wl;
		} popup;
		struct {
			struct zwlr_layer_surface_v1 *wl;
		} layer;
	} role;
	uint32_t frame;
	struct {
		nwl_surface_destroy_t destroy;
		nwl_surface_input_pointer_t input_pointer;
		nwl_surface_input_keyboard_t input_keyboard;
		void (*dnd)(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_dnd_event *event);
		nwl_surface_configure_t configure;
		void (*close)(struct nwl_surface *surface);
	} impl;
	void *userdata;
};

struct nwl_surface *nwl_surface_create(struct nwl_state *state, const char *title);
void nwl_surface_destroy(struct nwl_surface *surface);
void nwl_surface_destroy_later(struct nwl_surface *surface);
bool nwl_surface_set_vp_destination(struct nwl_surface *surface, int32_t width, int32_t height);
void nwl_surface_set_size(struct nwl_surface *surface, uint32_t width, uint32_t height);
void nwl_surface_swapbuffers(struct nwl_surface *surface, int32_t x, int32_t y);
void nwl_surface_render(struct nwl_surface *surface);
void nwl_surface_set_need_draw(struct nwl_surface *surface, bool rendernow);
void nwl_surface_role_unset(struct nwl_surface *surface);

bool nwl_surface_role_subsurface(struct nwl_surface *surface, struct nwl_surface *parent);
bool nwl_surface_role_layershell(struct nwl_surface *surface, struct wl_output *output, uint32_t layer);
bool nwl_surface_role_toplevel(struct nwl_surface *surface);
bool nwl_surface_role_popup(struct nwl_surface *surface, struct nwl_surface *parent, struct xdg_positioner *positioner);
#endif
