#define _POSIX_C_SOURCE 200809L
#include <wayland-client-core.h>
#include <wayland-cursor.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"
#include "nwl/seat.h"
#include "nwl/config.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-output-unstable-v1.h"
#if NWL_HAS_SEAT
#include "cursor-shape-v1.h"
#endif
struct nwl_poll {
	int epfd;
	int dirt_eventfd;
	int numfds;
	struct epoll_event *ev;
	struct wl_list data; // nwl_poll_data
};

// in seat.c
void nwl_seat_add_data_device(struct nwl_seat *seat);
// in shm.c
void nwl_shm_add_listener(struct nwl_state *state);


static void handle_wm_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	UNUSED(data);
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	handle_wm_ping
};

static void handle_output_geometry(
		void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform) {
	// don't care
	UNUSED(data);
	UNUSED(wl_output);
	UNUSED(x);
	UNUSED(y);
	UNUSED(physical_width);
	UNUSED(physical_height);
	UNUSED(subpixel);
	UNUSED(make);
	UNUSED(model);
	UNUSED(transform);
}
static void handle_output_mode(void *data,
		struct wl_output *wl_output,
		uint32_t flags,
		int32_t width,
		int32_t height,
		int32_t refresh) {
	// don't care
	UNUSED(data);
	UNUSED(wl_output);
	UNUSED(flags);
	UNUSED(width);
	UNUSED(height);
	UNUSED(refresh);
}
static void handle_output_done(
		void *data,
		struct wl_output *wl_output) {
	UNUSED(wl_output);
	struct nwl_output *nwloutput = data;
	if (nwloutput->is_done > 0) {
		nwloutput->is_done--;
		if (!nwloutput->is_done && nwloutput->state->events.global_bound) {
			struct nwl_bound_global global = { .global.output = nwloutput, .kind = NWL_BOUND_GLOBAL_OUTPUT };
			nwloutput->state->events.global_bound(&global);
		}
	}
}

static void handle_output_scale(
		void *data,
		struct wl_output *wl_output,
		int32_t factor) {
	UNUSED(wl_output);
	struct nwl_output *output = data;
	output->scale = factor;
}

static void handle_output_name(void *data, struct wl_output *wl_output, const char *name) {
	UNUSED(wl_output);
	struct nwl_output *output = data;
	if (output->name) {
		free(output->name);
	}
	output->name = strdup(name);
}

static void handle_output_description(void *data, struct wl_output *wl_output, const char *description) {
	UNUSED(wl_output);
	struct nwl_output *output = data;
	if (output->description) {
		free(output->description);
	}
	output->description = strdup(description);
}

static const struct wl_output_listener output_listener = {
	handle_output_geometry,
	handle_output_mode,
	handle_output_done,
	handle_output_scale,
	handle_output_name,
	handle_output_description,
};

static void handle_xdg_output_logical_position(void *data, struct zxdg_output_v1 *output,
		int32_t x, int32_t y) {
	struct nwl_output *noutput = data;
	UNUSED(output);
	noutput->x = x;
	noutput->y = y;
}

static void handle_xdg_output_logical_size(void *data, struct zxdg_output_v1 *output,
		int32_t width, int32_t height) {
	struct nwl_output *noutput = data;
	UNUSED(output);
	noutput->width = width;
	noutput->height = height;
}

static void handle_xdg_output_done(void *data, struct zxdg_output_v1 *output) {
	// shouldn't be used
	UNUSED(data);
	UNUSED(output);
}

static void handle_xdg_output_name(void *data, struct zxdg_output_v1 *output, const char *name) {
	UNUSED(output);
	struct nwl_output *nwloutput = data;
	if (nwloutput->name) {
		// wl_output name is preferred
		return;
	}
	nwloutput->name = strdup(name);
}

static void handle_xdg_output_description(void *data, struct zxdg_output_v1 *output, const char *description) {
	// don't care, use wl_output description
	UNUSED(data);
	UNUSED(output);
	UNUSED(description);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	handle_xdg_output_logical_position,
	handle_xdg_output_logical_size,
	handle_xdg_output_done,
	handle_xdg_output_name,
	handle_xdg_output_description
};

static void nwl_output_destroy(void *glob) {
	struct nwl_output *output = glob;
	if (output->state->events.global_destroy) {
		struct nwl_bound_global global = { .global.output = output, .kind = NWL_BOUND_GLOBAL_OUTPUT };
		output->state->events.global_destroy(&global);
	}
	if (output->name) {
		free(output->name);
	}
	if (output->description) {
		free(output->description);
	}
	if (output->xdg_output) {
		zxdg_output_v1_destroy(output->xdg_output);
	}
	wl_list_remove(&output->link);
	wl_output_set_user_data(output->output, NULL);
	wl_output_destroy(output->output);
	free(output);
}

static void nwl_output_create(struct wl_output *output, struct nwl_state *state, uint32_t name) {
	struct nwl_output *nwloutput = calloc(1, sizeof(struct nwl_output));
	wl_output_add_listener(output, &output_listener, nwloutput);
	wl_output_set_user_data(output, nwloutput);
	nwloutput->output = output;
	nwloutput->state = state;
	nwloutput->is_done = 1;
	wl_list_insert(&state->outputs, &nwloutput->link);
	struct nwl_global *glob = calloc(1, sizeof(struct nwl_global));
	glob->global = nwloutput;
	glob->name = name;
	glob->impl.destroy = nwl_output_destroy;
	if (state->wl.xdg_output_manager) {
		nwloutput->is_done = 2;
		nwloutput->xdg_output = zxdg_output_manager_v1_get_xdg_output(state->wl.xdg_output_manager, output);
		zxdg_output_v1_add_listener(nwloutput->xdg_output, &xdg_output_listener, nwloutput);
	}
	wl_list_insert(&state->globals, &glob->link);
}

static void *nwl_registry_bind(struct wl_registry *reg, uint32_t name,
		const struct wl_interface *interface, uint32_t version, uint32_t preferred_version) {
	uint32_t ver = version > preferred_version ? preferred_version : version;
	return wl_registry_bind(reg, name, interface, ver);
}

static void handle_global_add(void *data, struct wl_registry *reg,
		uint32_t name, const char *interface, uint32_t version) {
	struct nwl_state *state = data;
	if (state->events.global_add &&
			state->events.global_add(state, reg, name, interface, version)) {
		return;
	}
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->wl.compositor = nwl_registry_bind(reg, name, &wl_compositor_interface, version, 6);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->wl.layer_shell = nwl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, version, 4);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		state->wl.xdg_wm_base = nwl_registry_bind(reg, name, &xdg_wm_base_interface, version, 5);
		xdg_wm_base_add_listener(state->wl.xdg_wm_base, &wm_base_listener, state);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->wl.shm = nwl_registry_bind(reg, name, &wl_shm_interface, version, 1);
		nwl_shm_add_listener(state);
	} else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		state->wl.decoration = nwl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, version, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *newoutput = nwl_registry_bind(reg, name, &wl_output_interface, version, 4);
		nwl_output_create(newoutput, state, name);
	} else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
		state->wl.subcompositor = nwl_registry_bind(reg, name, &wl_subcompositor_interface, version, 1);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		state->wl.xdg_output_manager = nwl_registry_bind(reg, name, &zxdg_output_manager_v1_interface, version, 3);
	}
#if NWL_HAS_SEAT
	else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *newseat = nwl_registry_bind(reg, name, &wl_seat_interface, version, 8);
		nwl_seat_create(newseat, state, name);
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		state->wl.data_device_manager = nwl_registry_bind(reg, name, &wl_data_device_manager_interface, version, 3);
		// Maybe this global shows after seats?
		struct nwl_seat *seat;
		wl_list_for_each(seat, &state->seats, link) {
			if (seat->data_device.wl) {
				continue; // Will probably never happen, so why check?
			}
			nwl_seat_add_data_device(seat);
		}
	} else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
		state->wl.cursor_shape_manager = nwl_registry_bind(reg, name, &wp_cursor_shape_manager_v1_interface, version, 1);
	}
#endif
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	UNUSED(reg);
	struct nwl_state *state = data;
	if (state->events.global_remove) {
		state->events.global_remove(state,reg,name);
	}
	struct nwl_global *glob;
	wl_list_for_each(glob, &state->globals, link) {
		if (glob->name == name) {
			glob->impl.destroy(glob->global);
			wl_list_remove(&glob->link);
			free(glob);
			return;
		}
	}
}

static const struct wl_registry_listener reg_listener = {
	handle_global_add,
	handle_global_remove
};

struct nwl_poll_data {
	struct wl_list link;
	int fd;
	void *userdata;
	nwl_poll_callback_t callback;
};

void nwl_poll_add_fd(struct nwl_state *state, int fd, uint32_t events,
		nwl_poll_callback_t callback, void *data) {
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* ++state->poll->numfds);
	struct epoll_event ep;
	struct nwl_poll_data *polldata = calloc(1, sizeof(struct nwl_poll_data));
	wl_list_insert(&state->poll->data, &polldata->link);
	polldata->userdata = data;
	polldata->fd = fd;
	polldata->callback = callback;
	ep.data.ptr = polldata;
	ep.events = events;
	epoll_ctl(state->poll->epfd, EPOLL_CTL_ADD, fd, &ep);
}

void nwl_poll_del_fd(struct nwl_state *state, int fd) {
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* --state->poll->numfds);
	epoll_ctl(state->poll->epfd, EPOLL_CTL_DEL, fd, NULL);
	struct nwl_poll_data *data;
	wl_list_for_each(data, &state->poll->data, link) {
		if (data->fd == fd) {
			wl_list_remove(&data->link);
			free(data);
			return;
		}
	}
}

void surface_mark_dirty(struct nwl_surface *surface) {
	// This isn't really thread-safe!
	if (wl_list_empty(&surface->dirtlink)) {
		wl_list_insert(&surface->state->surfaces_dirty, &surface->dirtlink);
	}
	eventfd_write(surface->state->poll->dirt_eventfd, 1);
}

static void nwl_wayland_poll_display(struct nwl_state *state, uint32_t events, void *data) {
	UNUSED(data);
	UNUSED(events);
	if (wl_display_dispatch(state->wl.display) == -1) {
		perror("Fatal Wayland error");
		state->has_errored = true;
		state->num_surfaces = 0;
		state->run_with_zero_surfaces = false;
	}
}

static bool handle_dirty_surfaces(struct nwl_state *state) {
	struct nwl_surface *surface, *stmp;
	wl_list_for_each_safe(surface, stmp, &state->surfaces_dirty, dirtlink) {
		if (surface->states & NWL_SURFACE_STATE_DESTROY) {
			nwl_surface_destroy(surface);
			// This might have destroyed other surfaces. Start over to be safe!
			return true;
		} else if (surface->states & NWL_SURFACE_STATE_NEEDS_UPDATE && !surface->wl.frame_cb) {
			nwl_surface_update(surface);
		}
		wl_list_remove(&surface->dirtlink);
		wl_list_init(&surface->dirtlink); // To make sure it's not in an undefined state..
	}
	return false;
}

static void nwl_wayland_handle_dirt(struct nwl_state *state, uint32_t events, void *data) {
	UNUSED(data);
	UNUSED(events);
	// Yeah sure, errors might happen. Who cares?
	eventfd_read(state->poll->dirt_eventfd, NULL);
	while(handle_dirty_surfaces(state)) { }
}

bool nwl_poll_dispatch(struct nwl_state *state, int timeout) {
	wl_display_flush(state->wl.display);
	int nfds = epoll_wait(state->poll->epfd, state->poll->ev, state->poll->numfds, timeout);
	if (nfds == -1 && errno != EINTR) {
		perror("error while polling");
		return false;
	}
	for (int i = 0; i < nfds; i++) {
		struct nwl_poll_data *data = state->poll->ev[i].data.ptr;
		data->callback(state, state->poll->ev[i].events, data->userdata);
	}
	return true;
}

void nwl_wayland_run(struct nwl_state *state) {
	// Everything about this seems very flaky.. but it works!
	while (state->run_with_zero_surfaces || state->num_surfaces) {
		if (!nwl_poll_dispatch(state, -1)) {
			return;
		}
	}
}

int nwl_poll_get_fd(struct nwl_state *state) {
	if (state->poll) {
		return state->poll->epfd;
	}
	return -1;
}

char nwl_wayland_init(struct nwl_state *state) {
	wl_list_init(&state->seats);
	wl_list_init(&state->outputs);
	wl_list_init(&state->surfaces);
	wl_list_init(&state->surfaces_dirty);
	wl_list_init(&state->globals);
	wl_list_init(&state->subs);
	state->wl.display = wl_display_connect(NULL);
	if (!state->wl.display) {
		fprintf(stderr, "Couldn't connect to Wayland compositor.\n");
		return 1;
	}
	state->wl.registry = wl_display_get_registry(state->wl.display);
	wl_registry_add_listener(state->wl.registry, &reg_listener, state);
	if (wl_display_roundtrip(state->wl.display) == -1) {
		fprintf(stderr, "Initial roundtrip failed.\n");
		wl_registry_destroy(state->wl.registry);
		wl_display_disconnect(state->wl.display);
		return 1;
	}

	state->poll = calloc(1, sizeof(struct nwl_poll));
	wl_list_init(&state->poll->data);
	state->poll->epfd = epoll_create1(0);
	state->poll->dirt_eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	nwl_poll_add_fd(state, wl_display_get_fd(state->wl.display), EPOLLIN, nwl_wayland_poll_display, NULL);
	nwl_poll_add_fd(state, state->poll->dirt_eventfd, EPOLLIN, nwl_wayland_handle_dirt, NULL);

	// Ask xdg output manager for xdg_outputs in case wl_output globals were sent before it.
	if (state->wl.xdg_output_manager) {
		struct nwl_output *nwloutput;
		wl_list_for_each(nwloutput, &state->outputs, link) {
			if (nwloutput->xdg_output) {
				continue;
			}
			nwloutput->is_done = 2;
			nwloutput->xdg_output = zxdg_output_manager_v1_get_xdg_output(state->wl.xdg_output_manager, nwloutput->output);
			zxdg_output_v1_add_listener(nwloutput->xdg_output, &xdg_output_listener, nwloutput);
		}
	}
	// Extra roundtrip so output information is properly filled in
	wl_display_roundtrip(state->wl.display);
	return 0;
}

static void poll_destroy(struct nwl_poll *poll) {
	struct nwl_poll_data *data, *tmp;
	wl_list_for_each_safe(data, tmp, &poll->data, link) {
		free(data);
	}
	poll->numfds = 0;
	if (poll->ev) {
		free(poll->ev);
	}
	close(poll->epfd);
	close(poll->dirt_eventfd);
}

void nwl_wayland_uninit(struct nwl_state *state) {
	while (!wl_list_empty(&state->surfaces)) {
		struct nwl_surface *surface = wl_container_of(state->surfaces.next, surface, link);
		nwl_surface_destroy(surface);
	}
	struct nwl_state_sub *sub, *subtmp;
	wl_list_for_each_safe(sub, subtmp, &state->subs, link) {
		wl_list_remove(&sub->link);
		sub->impl->destroy(sub);
	}
	struct nwl_global *glob, *globtmp;
	wl_list_for_each_safe(glob, globtmp, &state->globals, link) {
		glob->impl.destroy(glob->global);
		wl_list_remove(&glob->link);
		free(glob);
	}
	// This should be moved out of here.
#if NWL_HAS_SEAT
	if (state->cursor_theme) {
		wl_cursor_theme_destroy(state->cursor_theme);
	}
	if (state->wl.cursor_shape_manager) {
		wp_cursor_shape_manager_v1_destroy(state->wl.cursor_shape_manager);
	}
#endif
	if (state->wl.compositor) {
		wl_compositor_destroy(state->wl.compositor);
	}
	if (state->wl.decoration) {
		zxdg_decoration_manager_v1_destroy(state->wl.decoration);
	}
	if (state->wl.layer_shell) {
		zwlr_layer_shell_v1_destroy(state->wl.layer_shell);
	}
	if (state->wl.registry) {
		wl_registry_destroy(state->wl.registry);
	}
	if (state->wl.subcompositor) {
		wl_subcompositor_destroy(state->wl.subcompositor);
	}
	if (state->wl.shm) {
		wl_shm_destroy(state->wl.shm);
	}
	if (state->wl.xdg_output_manager) {
		zxdg_output_manager_v1_destroy(state->wl.xdg_output_manager);
	}
	if (state->wl.xdg_wm_base) {
		xdg_wm_base_destroy(state->wl.xdg_wm_base);
	}
	if (state->wl.data_device_manager) {
		wl_data_device_manager_destroy(state->wl.data_device_manager);
	}
	wl_display_disconnect(state->wl.display);
	poll_destroy(state->poll);
	free(state->poll);
}

// These should be moved into state.c or something..
struct nwl_state_sub *nwl_state_get_sub(struct nwl_state *state, const struct nwl_state_sub_impl *subimpl) {
	struct nwl_state_sub *sub;
	wl_list_for_each(sub, &state->subs, link) {
		if (sub->impl == subimpl) {
			return sub;
		}
	}
	return NULL; // EVIL NULL POINTER!
}

void nwl_state_add_sub(struct nwl_state *state, struct nwl_state_sub *sub) {
	wl_list_insert(&state->subs, &sub->link);
}
