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
#include "nwl/config.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-output-unstable-v1.h"
#if NWL_HAS_SEAT
#include "nwl/seat.h"
#include "cursor-shape-v1.h"
#endif

// in seat.c
void nwl_seat_add_data_device(struct nwl_seat *seat);
// in shm.c
void nwl_shm_add_listener(struct nwl_core *core);

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
		// Notify somehow?
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

void nwl_output_deinit(struct nwl_output *output) {
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
}

void nwl_output_init(struct nwl_output *output, struct nwl_core *core, struct wl_output *wl_output) {
	wl_output_add_listener(wl_output, &output_listener, output);
	wl_output_set_user_data(wl_output, output);
	output->output = wl_output;
	output->core = core;
	output->is_done = 1;
	output->name = NULL;
	output->description = NULL;
	output->xdg_output = NULL;
	wl_list_insert(&core->outputs, &output->link);
	if (core->wl.xdg_output_manager) {
		output->is_done = 2;
		output->xdg_output = zxdg_output_manager_v1_get_xdg_output(core->wl.xdg_output_manager, wl_output);
		zxdg_output_v1_add_listener(output->xdg_output, &xdg_output_listener, output);
	}
}

static void *nwl_registry_bind(struct wl_registry *reg, uint32_t name,
		const struct wl_interface *interface, uint32_t version, uint32_t preferred_version) {
	uint32_t ver = version > preferred_version ? preferred_version : version;
	return wl_registry_bind(reg, name, interface, ver);
}

bool nwl_core_handle_global(struct nwl_core *core, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		core->wl.compositor = nwl_registry_bind(registry, name, &wl_compositor_interface, version, 6);
		return true;
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		core->wl.layer_shell = nwl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version, 4);
		return true;
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		core->wl.xdg_wm_base = nwl_registry_bind(registry, name, &xdg_wm_base_interface, version, 5);
		xdg_wm_base_add_listener(core->wl.xdg_wm_base, &wm_base_listener, core);
		return true;
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		core->wl.shm = nwl_registry_bind(registry, name, &wl_shm_interface, version, 1);
		nwl_shm_add_listener(core);
		return true;
	} else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		core->wl.decoration = nwl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, version, 1);
		return true;
	} else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
		core->wl.subcompositor = nwl_registry_bind(registry, name, &wl_subcompositor_interface, version, 1);
		return true;
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		core->wl.xdg_output_manager = nwl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, version, 3);
		return true;
	}
#if NWL_HAS_SEAT
	else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		core->wl.data_device_manager = nwl_registry_bind(registry, name, &wl_data_device_manager_interface, version, 3);
		return true;
	} else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
		core->wl.cursor_shape_manager = nwl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, version, 2);
		return true;
	}
#endif
	return false;
}

struct nwl_easy_output {
	struct nwl_output output;
	struct nwl_easy_global global;
	bool announced;
};


static void nwl_easy_output_destroy(struct nwl_easy_global *global) {
	struct nwl_easy_output *output = wl_container_of(global, output, global);
	struct nwl_easy *easy = wl_container_of(output->output.core, easy, core);
	if (easy->events.global_destroy) {
		struct nwl_bound_global global = { .global.output = &output->output, .kind = NWL_BOUND_GLOBAL_OUTPUT };
		easy->events.global_destroy(&global);
	}
	nwl_output_deinit(&output->output);
	free(output);
}

#if NWL_HAS_SEAT

struct nwl_easy_seat {
	struct nwl_seat seat;
	struct nwl_easy_global global;
};

static void easy_seat_destroy(struct nwl_easy_global *global) {
	struct nwl_easy_seat *seat = wl_container_of(global, seat, global);
	struct nwl_easy *easy = wl_container_of(seat->seat.core, easy, core);
	if (easy->events.global_destroy) {
		struct nwl_bound_global global = { .global.seat = &seat->seat, .kind = NWL_BOUND_GLOBAL_SEAT };
		easy->events.global_destroy(&global);
	}
	nwl_easy_del_fd(easy, seat->seat.keyboard_repeat_fd);
	nwl_seat_deinit(&seat->seat);
	free(seat);
}
static void easy_handle_repeat(struct nwl_easy *easy, uint32_t events, void *data) {
	UNUSED(easy);
	UNUSED(events);
	struct nwl_seat *seat = data;
	nwl_seat_handle_repeat(seat);
}
#endif

static void handle_global_add(void *data, struct wl_registry *reg,
		uint32_t name, const char *interface, uint32_t version) {
	struct nwl_easy *easy = data;
	if (easy->events.global_add &&
			easy->events.global_add(easy, reg, name, interface, version)) {
		return;
	}
	if (nwl_core_handle_global(&easy->core, reg, name, interface, version)) {
		return;
	}
	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *wl_output = nwl_registry_bind(reg, name, &wl_output_interface, version, 4);
		struct nwl_easy_output *output = calloc(1, sizeof(struct nwl_easy_output));
		nwl_output_init(&output->output, &easy->core, wl_output);
		output->global.name = name;
		output->global.impl.destroy = nwl_easy_output_destroy;
		wl_list_insert(&easy->globals, &output->global.link);
		easy->has_new_outputs = true;
	}
#if NWL_HAS_SEAT
	else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct nwl_easy_seat *seat = calloc(1, sizeof(struct nwl_easy_seat));
		seat->global.name = name;
		seat->global.impl.destroy = easy_seat_destroy;
		wl_list_insert(&easy->globals, &seat->global.link);
		struct wl_seat *newseat = nwl_registry_bind(reg, name, &wl_seat_interface, version, 8);
		nwl_seat_init(&seat->seat, newseat, &easy->core);
		nwl_easy_add_fd(easy, seat->seat.keyboard_repeat_fd, 1, easy_handle_repeat, &seat->seat);
		if (easy->events.global_bound) {
			struct nwl_bound_global global = { .global.seat = &seat->seat, .kind = NWL_BOUND_GLOBAL_SEAT };
			easy->events.global_bound(&global);
		}
	} else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		easy->core.wl.data_device_manager = nwl_registry_bind(reg, name, &wl_data_device_manager_interface, version, 3);
	}
#endif
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	UNUSED(reg);
	struct nwl_easy *easy = data;
	if (easy->events.global_remove) {
		easy->events.global_remove(easy, reg, name);
	}
	struct nwl_easy_global *glob;
	wl_list_for_each(glob, &easy->globals, link) {
		if (glob->name == name) {
			wl_list_remove(&glob->link);
			glob->impl.destroy(glob);
			return;
		}
	}
}

static const struct wl_registry_listener reg_listener = {
	handle_global_add,
	handle_global_remove
};

void nwl_easy_add_fd(struct nwl_easy *easy, int fd, uint32_t events,
		nwl_poll_callback_t callback, void *data) {
	easy->poll.ev = realloc(easy->poll.ev, sizeof(struct epoll_event)* ++easy->poll.numfds);
	struct epoll_event ep;
	struct nwl_poll_data *polldata = calloc(1, sizeof(struct nwl_poll_data));
	wl_list_insert(&easy->poll.data, &polldata->link);
	polldata->userdata = data;
	polldata->fd = fd;
	polldata->callback = callback;
	ep.data.ptr = polldata;
	ep.events = events;
	epoll_ctl(easy->poll.epfd, EPOLL_CTL_ADD, fd, &ep);
}

void nwl_easy_del_fd(struct nwl_easy *easy, int fd) {
	easy->poll.ev = realloc(easy->poll.ev, sizeof(struct epoll_event)* --easy->poll.numfds);
	epoll_ctl(easy->poll.epfd, EPOLL_CTL_DEL, fd, NULL);
	struct nwl_poll_data *data;
	wl_list_for_each(data, &easy->poll.data, link) {
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
		wl_list_insert(&surface->core->surfaces_dirty, &surface->dirtlink);
	}
	surface->core->has_dirty_surfaces = true;
}

static void nwl_wayland_poll_display(struct nwl_easy *easy, uint32_t events, void *data) {
	UNUSED(data);
	UNUSED(events);
	if (wl_display_dispatch(easy->display) == -1) {
		perror("Fatal Wayland error");
		easy->has_errored = true;
		easy->core.num_surfaces = 0;
		easy->run_with_zero_surfaces = false;
	}
}

static void handle_dirty_surfaces(struct nwl_core *core) {
	struct nwl_surface *surface, *stmp;
	core->has_dirty_surfaces = false;
	wl_list_for_each_safe(surface, stmp, &core->surfaces_dirty, dirtlink) {
		if (surface->states & NWL_SURFACE_STATE_DESTROY) {
			nwl_surface_destroy(surface);
			// This might have destroyed other surfaces. Start over to be safe!
			core->has_dirty_surfaces = true;
			return;
		} else if (surface->states & NWL_SURFACE_STATE_NEEDS_UPDATE && !surface->wl.frame_cb) {
			nwl_surface_update(surface);
		}
		wl_list_remove(&surface->dirtlink);
		wl_list_init(&surface->dirtlink); // To make sure it's not in an undefined state..
	}
}

void nwl_core_handle_dirt(struct nwl_core *core) {
	while(core->has_dirty_surfaces) {
		handle_dirty_surfaces(core);
	}
}

void announce_outputs(struct nwl_easy *easy) {
	struct nwl_output *output;
	bool roundtripped = false;
	wl_list_for_each(output, &easy->core.outputs, link) {
		struct nwl_easy_output *easyoutput = wl_container_of(output, easyoutput, output);
		if (!easyoutput->announced) {
			if (easyoutput->output.is_done != 0 && !roundtripped) {
				// Roundtrip to ensure it's done. Is this dangerous?
				wl_display_roundtrip(easy->display);
				roundtripped = true;
			}
			easyoutput->announced = true;
			if (easy->events.global_bound != NULL) {
				struct nwl_bound_global glob = {
					.global.output = output,
					.kind = NWL_BOUND_GLOBAL_OUTPUT,
				};
				easy->events.global_bound(&glob);
			}
		}
	}
}

bool nwl_easy_dispatch(struct nwl_easy *easy, int timeout) {
	wl_display_flush(easy->display);
	int nfds = epoll_wait(easy->poll.epfd, easy->poll.ev, easy->poll.numfds, timeout);
	if (nfds == -1 && errno != EINTR) {
		perror("error while polling");
		return false;
	}
	for (int i = 0; i < nfds; i++) {
		struct nwl_poll_data *data = easy->poll.ev[i].data.ptr;
		data->callback(easy, easy->poll.ev[i].events, data->userdata);
	}
	if (easy->has_new_outputs) {
		announce_outputs(easy);
		easy->has_new_outputs = false;
	}
	if (easy->core.has_dirty_surfaces) {
		nwl_core_handle_dirt(&easy->core);
	}
	return true;
}

void nwl_easy_run(struct nwl_easy *easy) {
	// Everything about this seems very flaky.. but it works!
	while (easy->run_with_zero_surfaces || easy->core.num_surfaces) {
		if (!nwl_easy_dispatch(easy, -1)) {
			return;
		}
	}
}

void nwl_core_init(struct nwl_core *core) {
	wl_list_init(&core->seats);
	wl_list_init(&core->outputs);
	wl_list_init(&core->surfaces);
	wl_list_init(&core->surfaces_dirty);
	wl_list_init(&core->subs);
}

bool nwl_easy_init(struct nwl_easy *easy) {
	nwl_core_init(&easy->core);
	wl_list_init(&easy->globals);
	wl_list_init(&easy->poll.data);
	easy->display = wl_display_connect(NULL);
	if (!easy->display) {
		fprintf(stderr, "Couldn't connect to Wayland compositor.\n");
		return false;
	}
	easy->registry = wl_display_get_registry(easy->display);
	wl_registry_add_listener(easy->registry, &reg_listener, easy);
	easy->poll.epfd = epoll_create1(0);
	easy->poll.ev = NULL;
	if (wl_display_roundtrip(easy->display) == -1) {
		fprintf(stderr, "Initial roundtrip failed.\n");
		wl_registry_destroy(easy->registry);
		wl_display_disconnect(easy->display);
		return false;
	}
	nwl_easy_add_fd(easy, wl_display_get_fd(easy->display), EPOLLIN, nwl_wayland_poll_display, NULL);

	// Ask xdg output manager for xdg_outputs in case wl_output globals were sent before it.
	if (easy->core.wl.xdg_output_manager) {
		struct nwl_output *nwloutput;
		wl_list_for_each(nwloutput, &easy->core.outputs, link) {
			if (nwloutput->xdg_output) {
				continue;
			}
			nwloutput->is_done = 2;
			nwloutput->xdg_output = zxdg_output_manager_v1_get_xdg_output(easy->core.wl.xdg_output_manager, nwloutput->output);
			zxdg_output_v1_add_listener(nwloutput->xdg_output, &xdg_output_listener, nwloutput);
		}
	}
#if NWL_HAS_SEAT
	if (easy->core.wl.data_device_manager) {
		struct nwl_seat *seat;
		wl_list_for_each((seat), &easy->core.seats, link) {
			if (seat->data_device.wl) {
				continue;
			}
			seat->data_device.wl = wl_data_device_manager_get_data_device(easy->core.wl.data_device_manager, seat->wl_seat);
		}
	}
#endif
	// Extra roundtrip so output information is properly filled in
	wl_display_roundtrip(easy->display);
	// Let them know there are outputs!
	if (easy->has_new_outputs) {
		announce_outputs(easy);
		easy->has_new_outputs = false;
	}
	return true;
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
}

void nwl_core_deinit(struct nwl_core *core) {
	while (!wl_list_empty(&core->surfaces)) {
		struct nwl_surface *surface = wl_container_of(core->surfaces.next, surface, link);
		nwl_surface_destroy(surface);
	}
	struct nwl_core_sub *sub, *subtmp;
	wl_list_for_each_safe(sub, subtmp, &core->subs, link) {
		wl_list_remove(&sub->link);
		sub->impl->destroy(sub);
	}
	// This should be moved out of here.
#if NWL_HAS_SEAT
	if (core->cursor_theme) {
		wl_cursor_theme_destroy(core->cursor_theme);
	}
	if (core->wl.cursor_shape_manager) {
		wp_cursor_shape_manager_v1_destroy(core->wl.cursor_shape_manager);
	}
#endif
	if (core->wl.compositor) {
		wl_compositor_destroy(core->wl.compositor);
	}
	if (core->wl.decoration) {
		zxdg_decoration_manager_v1_destroy(core->wl.decoration);
	}
	if (core->wl.layer_shell) {
		zwlr_layer_shell_v1_destroy(core->wl.layer_shell);
	}
	if (core->wl.subcompositor) {
		wl_subcompositor_destroy(core->wl.subcompositor);
	}
	if (core->wl.shm) {
		wl_shm_destroy(core->wl.shm);
	}
	if (core->wl.xdg_output_manager) {
		zxdg_output_manager_v1_destroy(core->wl.xdg_output_manager);
	}
	if (core->wl.xdg_wm_base) {
		xdg_wm_base_destroy(core->wl.xdg_wm_base);
	}
	if (core->wl.data_device_manager) {
		wl_data_device_manager_destroy(core->wl.data_device_manager);
	}
}

void nwl_easy_deinit(struct nwl_easy *easy) {
	struct nwl_easy_global *glob, *globtmp;
	wl_list_for_each_safe(glob, globtmp, &easy->globals, link) {
		wl_list_remove(&glob->link);
		glob->impl.destroy(glob);
	}
	nwl_core_deinit(&easy->core);
	if (easy->registry) {
		wl_registry_destroy(easy->registry);
	}
	wl_display_disconnect(easy->display);
	poll_destroy(&easy->poll);
}

// These should be moved into state.c or something..
struct nwl_core_sub *nwl_core_get_sub(struct nwl_core *core, const struct nwl_core_sub_impl *subimpl) {
	struct nwl_core_sub *sub;
	wl_list_for_each(sub, &core->subs, link) {
		if (sub->impl == subimpl) {
			return sub;
		}
	}
	return NULL; // EVIL NULL POINTER!
}

void nwl_core_add_sub(struct nwl_core *core, struct nwl_core_sub *sub) {
	wl_list_insert(&core->subs, &sub->link);
}
