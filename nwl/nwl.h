#ifndef _NWL_NWL_H
#define _NWL_NWL_H
#define UNUSED(x) (void)(x)
#include <stdbool.h>
#include <wayland-util.h>

struct wl_registry;

struct nwl_output {
	struct nwl_core *core;
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
struct nwl_core_sub {
	struct wl_list link;
	const struct nwl_core_sub_impl *impl;
};

struct nwl_core_sub_impl {
	void (*destroy)(struct nwl_core_sub *sub);
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

struct nwl_core {
	struct {
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
	struct wl_list subs; // nwl_core_sub

	struct wl_cursor_theme *cursor_theme;
	uint32_t cursor_theme_size;
	uint32_t num_surfaces;
	// When true, call nwl_core_handle_dirt
	bool has_dirty_surfaces;
	const char *xdg_app_id; // This app_id is conveniently automagically set on xdg_toplevels, if not null
};

struct nwl_poll {
	int epfd;
	int numfds;
	struct epoll_event *ev;
	struct wl_list data; // nwl_poll_data
};

struct nwl_easy {
	struct nwl_core core;
	struct nwl_poll poll;
	struct {
		// A global has been bound by nwl_easy!
		// kind is nwl_bound_global_kind
		void (*global_bound)(const struct nwl_bound_global *global);
		// The global is about to be destroyed!
		void (*global_destroy)(const struct nwl_bound_global *global);
		// A wild global appears! Return true to not have nwl_easy take care of it!
		bool (*global_add)(struct nwl_easy *easy, struct wl_registry *registry, uint32_t name,
			const char *interface, uint32_t version);
		// The global disappears!
		void (*global_remove)(struct nwl_easy *easy, struct wl_registry *registry, uint32_t name);
	} events;
	struct wl_list globals; // nwl_easy_global

	struct wl_registry *registry;
	struct wl_display *display;
	bool run_with_zero_surfaces;
	bool has_errored;
	bool has_new_outputs;
};

struct nwl_easy_global {
	struct wl_list link;
	uint32_t name;
	struct {
		void (*destroy)(struct nwl_easy_global* global);
	} impl;
};


typedef void (*nwl_poll_callback_t)(struct nwl_easy *easy, uint32_t events, void* data);

struct nwl_poll_data {
	struct wl_list link;
	int fd;
	void *userdata;
	nwl_poll_callback_t callback;
};

void nwl_output_init(struct nwl_output *output, struct nwl_core *core, struct wl_output *wl_output);
// Destroys the wl_global as well!
void nwl_output_deinit(struct nwl_output *output);

void nwl_core_init(struct nwl_core *core);
void nwl_core_deinit(struct nwl_core *core);
void nwl_core_handle_dirt(struct nwl_core *core);
void nwl_core_add_sub(struct nwl_core *core, struct nwl_core_sub *sub);
struct nwl_core_sub *nwl_core_get_sub(struct nwl_core *core, const struct nwl_core_sub_impl *subimpl);

bool nwl_easy_init(struct nwl_easy *easy);
void nwl_easy_deinit(struct nwl_easy *easy);
void nwl_easy_run(struct nwl_easy *easy);
void nwl_easy_add_fd(struct nwl_easy *easy, int fd, uint32_t events,
	nwl_poll_callback_t callback, void *data);
void nwl_easy_del_fd(struct nwl_easy *easy, int fd);
bool nwl_easy_dispatch(struct nwl_easy *easy, int timeout);
#endif
