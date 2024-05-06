#ifndef _NWL_NWL_H
#define _NWL_NWL_H
#define UNUSED(x) (void)(x)
#include <stdbool.h>
#include <wayland-util.h>

struct nwl_global {
	struct wl_list link;
	uint32_t name;
	void *global;
	struct {
		void (*destroy)(void* global);
	} impl;
};

struct nwl_output {
	struct nwl_state *state;
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	struct wl_list link;
	int scale;
	uint8_t is_done;
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	char *name;
	char *description;
};

// Need a better name for these
struct nwl_state_sub {
	struct wl_list link;
	const struct nwl_state_sub_impl *impl;
};

struct nwl_state_sub_impl {
	void (*destroy)(struct nwl_state_sub *sub);
};

enum nwl_bound_global_kind {
	NWL_BOUND_GLOBAL_OUTPUT = 0,
	NWL_BOUND_GLOBAL_SEAT
};

struct nwl_bound_global {
	enum nwl_bound_global_kind kind;
	union {
		struct nwl_output *output;
		struct nwl_seat *seat;
	} global;
};

struct nwl_state {
	struct {
		struct wl_display *display;
		struct wl_registry *registry;
		struct wl_compositor *compositor;
		struct wl_shm *shm;
		struct xdg_wm_base *xdg_wm_base;
		struct zxdg_output_manager_v1 *xdg_output_manager;
		struct zwlr_layer_shell_v1 *layer_shell;
		struct zxdg_decoration_manager_v1 *decoration;
		struct wl_subcompositor *subcompositor;
		struct wl_data_device_manager *data_device_manager;
		struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
	} wl;
	struct wl_list seats; // nwl_seat
	struct wl_list outputs; // nwl_output
	struct wl_list surfaces; // nwl_surface
	struct wl_list surfaces_dirty; // nwl_surface dirtlink
	struct wl_list globals; // nwl_global
	struct wl_list subs; // nwl_state_sub

	struct wl_cursor_theme *cursor_theme;
	uint32_t cursor_theme_size;
	uint32_t num_surfaces;
	bool run_with_zero_surfaces;
	bool has_errored;
	struct nwl_poll *poll;
	struct {
		// A global has been bound by nwl!
		// kind is nwl_bound_global_kind
		void (*global_bound)(const struct nwl_bound_global *global);
		// The global is about to be destroyed!
		void (*global_destroy)(const struct nwl_bound_global *global);
		// A wild global appears! Return true to not have nwl take care of it!
		bool (*global_add)(struct nwl_state *state, struct wl_registry *registry, uint32_t name,
			const char *interface, uint32_t version);
		// The global disappears!
		void (*global_remove)(struct nwl_state *state, struct wl_registry *registry, uint32_t name);
	} events;
	const char *xdg_app_id; // This app_id is conveniently automagically set on xdg_toplevels, if not null
};

typedef void (*nwl_poll_callback_t)(struct nwl_state *state, uint32_t events, void* data);

char nwl_wayland_init(struct nwl_state *state);
void nwl_wayland_uninit(struct nwl_state *state);
void nwl_wayland_run(struct nwl_state *state);
void nwl_state_add_sub(struct nwl_state *state, struct nwl_state_sub *sub);
struct nwl_state_sub *nwl_state_get_sub(struct nwl_state *state, const struct nwl_state_sub_impl *subimpl);
void nwl_poll_add_fd(struct nwl_state *state, int fd, uint32_t events,
	nwl_poll_callback_t callback, void *data);
bool nwl_poll_dispatch(struct nwl_state *state, int timeout);
void nwl_poll_del_fd(struct nwl_state *state, int fd);
int nwl_poll_get_fd(struct nwl_state *state);
void nwl_state_global_add(struct nwl_state *state, struct nwl_global global);

#endif
