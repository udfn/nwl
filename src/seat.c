#define _POSIX_C_SOURCE 200809L
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <string.h>
#include <unistd.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"
#include "nwl/seat.h"
#include "cursor-shape-v1.h"

static const char *get_env_locale() {
	const char *loc;
	if ((loc = getenv("LC_ALL")) && *loc)
		return loc;
	if ((loc = getenv("LC_CTYPE")) && *loc)
		return loc;
	if ((loc = getenv("LANG")) && *loc)
		return loc;
	return "C";
}

struct nwl_xkb_context {
	struct nwl_core_sub sub;
	struct xkb_context *ctx;
};

static void xkb_context_sub_destroy(struct nwl_core_sub *sub) {
	struct nwl_xkb_context *xkbsub = wl_container_of(sub, xkbsub, sub);
	xkb_context_unref(xkbsub->ctx);
	free(xkbsub);
}

static const struct nwl_core_sub_impl xkb_context_sub_impl = {
	xkb_context_sub_destroy
};

static struct xkb_context *get_xkb_context(struct nwl_core *core) {
	struct nwl_core_sub *exist = nwl_core_get_sub(core, &xkb_context_sub_impl);
	if (exist) {
		struct nwl_xkb_context *xkbsub = wl_container_of(exist, xkbsub, sub);
		return xkbsub->ctx;
	}
	struct nwl_xkb_context *xkbsub = malloc(sizeof(struct nwl_xkb_context));
	xkbsub->ctx = xkb_context_new(0);
	xkbsub->sub.impl = &xkb_context_sub_impl;
	nwl_core_add_sub(core, &xkbsub->sub);
	return xkbsub->ctx;
}

static void unref_seat_xkb(struct nwl_seat *seat) {
	if (seat->keyboard_xkb.keymap) {
		xkb_state_unref(seat->keyboard_xkb.state);
		xkb_keymap_unref(seat->keyboard_xkb.keymap);
	}
	if (seat->keyboard_xkb.compose_state) {
		xkb_compose_state_unref(seat->keyboard_xkb.compose_state);
	}
	if (seat->keyboard_xkb.compose_table) {
		xkb_compose_table_unref(seat->keyboard_xkb.compose_table);
	}
	seat->keyboard_xkb = (struct nwl_seat_keymap_xkb){0};
}

static void handle_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
		int32_t fd, uint32_t size) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	unref_seat_xkb(seat);
	if (format == WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP) {
		return;
	}
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		fprintf(stderr, "I don't understand keymap format %i :(\n", format);
		close(fd);
		return;
	}
	char *kbmap = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	struct xkb_context *context = get_xkb_context(seat->core);
	// -1 to remove the null termination
	seat->keyboard_xkb.keymap = xkb_keymap_new_from_buffer(context, kbmap, size-1,
		XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(kbmap, size);
	close(fd);

	seat->keyboard_xkb.state = xkb_state_new(seat->keyboard_xkb.keymap);

	seat->keyboard_xkb.compose_table = xkb_compose_table_new_from_locale(context,
		get_env_locale(), 0);
	if (seat->keyboard_xkb.compose_table) {
		seat->keyboard_xkb.compose_state = xkb_compose_state_new(seat->keyboard_xkb.compose_table, XKB_COMPOSE_STATE_NO_FLAGS);
	}
}

static void dispatch_keyboard_event(struct nwl_seat *seat) {
	struct nwl_surface *surface = seat->keyboard_focus;
	if (surface->impl.input_keyboard) {
		surface->impl.input_keyboard(surface, seat, seat->keyboard_event);
	}
	seat->keyboard_event->type = 0;
}

static void update_keyboard_event_compose(struct nwl_seat *seat) {
	struct nwl_keyboard_event *event = seat->keyboard_event;
	if (xkb_compose_state_feed(seat->keyboard_xkb.compose_state, event->keysym) == XKB_COMPOSE_FEED_ACCEPTED) {
		char status = xkb_compose_state_get_status(seat->keyboard_xkb.compose_state);
		switch (status) {
			case XKB_COMPOSE_COMPOSING:
				event->compose_state = NWL_KEYBOARD_COMPOSE_COMPOSING;
				break;
			case XKB_COMPOSE_COMPOSED:
				event->compose_state = NWL_KEYBOARD_COMPOSE_COMPOSED;
				event->keysym = xkb_compose_state_get_one_sym(seat->keyboard_xkb.compose_state);
				xkb_compose_state_get_utf8(seat->keyboard_xkb.compose_state, event->utf8, 16);
				xkb_compose_state_reset(seat->keyboard_xkb.compose_state);
				break;
			case XKB_COMPOSE_CANCELLED:
				xkb_compose_state_reset(seat->keyboard_xkb.compose_state);
				// fallthrough
			default:
				event->compose_state = NWL_KEYBOARD_COMPOSE_NONE;
		}
	}
}

void nwl_seat_handle_repeat(struct nwl_seat *seat) {
	uint64_t expirations;
	if (!seat->keyboard) {
		return;
	}
	read(seat->keyboard_repeat_fd, &expirations, sizeof(uint64_t));
	if (expirations == 0) {
		return;
	}
	if (!seat->keyboard_repeat_enabled) {
		struct itimerspec timer = { 0 };
		timerfd_settime(seat->keyboard_repeat_fd, 0, &timer, NULL);
		return;
	}
	if (seat->keyboard_focus) {
		seat->keyboard_event->type = NWL_KEYBOARD_EVENT_KEYREPEAT;
		if (seat->keyboard_compose_enabled && seat->keyboard_xkb.compose_state) {
			// Getting the sym here so the repeats go through the proper compose process
			seat->keyboard_event->keysym = xkb_state_key_get_one_sym(seat->keyboard_xkb.state, seat->keyboard_event->keycode);
			update_keyboard_event_compose(seat);
		}
		dispatch_keyboard_event(seat);
	}
}

static void handle_keyboard_enter(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface,
		struct wl_array *keys) {
	UNUSED(wl_keyboard);
	UNUSED(keys);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (!surface) {
		return;
	}
	seat->keyboard_focus = wl_surface_get_user_data(surface);
	seat->keyboard_event->serial = serial;
	seat->keyboard_event->focus = true;
	seat->keyboard_event->type = NWL_KEYBOARD_EVENT_FOCUS;
	dispatch_keyboard_event(seat);
	// TODO: held keys
}

static void handle_keyboard_leave(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface) {
	UNUSED(surface);
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (!seat->keyboard_focus) {
		return;
	}
	seat->keyboard_event->serial = serial;
	seat->keyboard_event->focus = false;
	seat->keyboard_event->type = NWL_KEYBOARD_EVENT_FOCUS;
	dispatch_keyboard_event(seat);
	seat->keyboard_focus = NULL;
	struct itimerspec timer = { 0 };
	timerfd_settime(seat->keyboard_repeat_fd, 0, &timer, NULL);
}

static void handle_keyboard_key(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state) {
	UNUSED(wl_keyboard);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (!seat->keyboard_focus) {
		return;
	}
	struct nwl_keyboard_event *event = seat->keyboard_event;
	event->keycode = key+8;
	if (seat->keyboard_xkb.state) {
		event->keysym = xkb_state_key_get_one_sym(seat->keyboard_xkb.state, event->keycode);
	}
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		event->type = NWL_KEYBOARD_EVENT_KEYDOWN;
		if (seat->keyboard_xkb.state) {
			if (seat->keyboard_xkb.compose_state && seat->keyboard_compose_enabled) {
				update_keyboard_event_compose(seat);
			} else {
				event->compose_state = NWL_KEYBOARD_COMPOSE_NONE;
			}
			if (event->compose_state != NWL_KEYBOARD_COMPOSE_COMPOSED) {
				xkb_state_key_get_utf8(seat->keyboard_xkb.state, event->keycode, event->utf8, 16);
			}
		}
	} else {
		event->type = NWL_KEYBOARD_EVENT_KEYUP;
	}
	event->serial = serial;
	dispatch_keyboard_event(seat);
	if (seat->keyboard_repeat_enabled && seat->keyboard_repeat_delay > 0 &&
			(seat->keyboard_xkb.keymap == NULL ||
			xkb_keymap_key_repeats(seat->keyboard_xkb.keymap, event->keycode))) {
		struct itimerspec timer = { 0 };
		if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
			timer.it_value.tv_nsec = seat->keyboard_repeat_delay * 1000000;
			timer.it_interval.tv_nsec = seat->keyboard_repeat_rate * 1000000;
		}
		timerfd_settime(seat->keyboard_repeat_fd, 0, &timer, NULL);
	}
}

static void handle_keyboard_modifiers(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (seat->keyboard_xkb.state) {
		xkb_state_update_mask(seat->keyboard_xkb.state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
	}
	seat->keyboard_event->serial = serial;
}

static void handle_keyboard_repeat(
		void *data,
		struct wl_keyboard *wl_keyboard,
		int32_t rate,
		int32_t delay) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	seat->keyboard_repeat_rate = rate;
	seat->keyboard_repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	handle_keyboard_keymap,
	handle_keyboard_enter,
	handle_keyboard_leave,
	handle_keyboard_key,
	handle_keyboard_modifiers,
	handle_keyboard_repeat
};

// It could be that not all cursor themes have all of these named like this..
// So should probably have multiple fallback names.
const char *xcursor_shapes[] = {
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT] = "default",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CONTEXT_MENU] = "context-menu",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_HELP] = "help",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER] = "pointer",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_PROGRESS] = "progress",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT] = "wait",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CELL] = "cell",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR] = "crosshair",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT] = "text",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_VERTICAL_TEXT] = "vertical-text",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALIAS] = "alias",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COPY] = "copy",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE] = "move",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NO_DROP] = "no-drop",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED] = "not-allowed",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB] = "grab",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING] = "grabbing",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE] = "e-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE] = "n-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE] = "ne-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE] = "nw-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE] = "s-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE] = "se-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE] = "sw-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE] = "w-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE] = "ew-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE] = "ns-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE] = "nesw-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE] = "nwse-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_COL_RESIZE] = "col-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ROW_RESIZE] = "row-resize",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL] = "all-scroll",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_IN] = "zoom-in",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ZOOM_OUT] = "zoom-out",
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DND_ASK] = "dnd-ask",
	// Not sure about this one...
	[WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE] = "size_all",
};

void nwl_seat_set_pointer_shape(struct nwl_seat *seat, enum wp_cursor_shape_device_v1_shape shape) {
	if (seat->core->wl.cursor_shape_manager) {
		if (!seat->pointer_surface.shape_device) {
			seat->pointer_surface.shape_device = wp_cursor_shape_manager_v1_get_pointer(seat->core->wl.cursor_shape_manager, seat->pointer);
		}
		wp_cursor_shape_device_v1_set_shape(seat->pointer_surface.shape_device, seat->pointer_event->serial, shape);
		seat->pointer_surface.nwl = NULL;
	} else {
		// Should probably check version..
		if (shape > 0 && shape <= WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_RESIZE) {
			nwl_seat_set_pointer_cursor(seat, xcursor_shapes[shape]);
		}
	}
}

void nwl_seat_set_pointer_cursor(struct nwl_seat *seat, const char *cursor) {
	if (cursor == NULL) {
		wl_pointer_set_cursor(seat->pointer, seat->pointer_event->serial, NULL, 0, 0);
		return;
	}
	struct nwl_surface *surface = seat->pointer_focus;
	if (!surface) {
		return;
	}
	if (!seat->pointer_surface.xcursor_surface) {
		seat->pointer_surface.xcursor_surface = wl_compositor_create_surface(seat->core->wl.compositor);
	}
	if ((int)seat->core->cursor_theme_size != surface->scale*24) {
		if (seat->core->cursor_theme) {
			wl_cursor_theme_destroy(seat->core->cursor_theme);
		}
		seat->core->cursor_theme = wl_cursor_theme_load(NULL, 24 * surface->scale, seat->core->wl.shm);
		seat->core->cursor_theme_size = 24 * surface->scale;
		wl_surface_set_buffer_scale(seat->pointer_surface.xcursor_surface, surface->scale);
	}
	struct wl_cursor *xcursor = wl_cursor_theme_get_cursor(seat->core->cursor_theme, cursor);
	if (!xcursor) {
		return;
	}
	struct wl_buffer *cursbuffer = wl_cursor_image_get_buffer(xcursor->images[0]);
	wl_surface_attach(seat->pointer_surface.xcursor_surface, cursbuffer, 0, 0);
	wl_surface_damage_buffer(seat->pointer_surface.xcursor_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(seat->pointer_surface.xcursor_surface);
	seat->pointer_surface.nwl = NULL;
	// Divide hotspot by scale, why? Because the compositor multiplies it by the scale!
	int32_t hot_x = xcursor->images[0]->hotspot_x/surface->scale;
	int32_t hot_y = xcursor->images[0]->hotspot_y/surface->scale;
	wl_pointer_set_cursor(seat->pointer, seat->pointer_event->serial, seat->pointer_surface.xcursor_surface,
		hot_x, hot_y);
}

static inline void resize_surface_to_desired(struct nwl_surface *surface, int scale) {
	if (surface->width != surface->desired_width ||
			surface->height != surface->desired_height || scale != surface->scale) {
		surface->width = surface->desired_width;
		surface->height = surface->desired_height;
		surface->scale = scale;
		surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	}
}

bool nwl_seat_set_pointer_surface(struct nwl_seat *seat, struct nwl_surface *surface, int32_t hotspot_x, int32_t hotspot_y) {
	if (surface == NULL) {
		// for consistency with nwl_seat_set_pointer_cursor
		wl_pointer_set_cursor(seat->pointer, seat->pointer_event->serial, NULL, 0, 0);
		return true;
	}
	if (!seat->pointer_focus || (surface->role_id && surface->role_id != NWL_SURFACE_ROLE_CURSOR)) {
		return false;
	}
	// Cursor surfaces can always be the desired size?
	resize_surface_to_desired(surface, seat->pointer_focus->scale);
	if (!surface->role_id) {
		surface->role_id = NWL_SURFACE_ROLE_CURSOR;
		wl_surface_commit(surface->wl.surface);
	}
	seat->pointer_surface.nwl = surface;
	wl_pointer_set_cursor(seat->pointer, seat->pointer_event->serial, surface->wl.surface, hotspot_x, hotspot_y);
	if (surface) {
		nwl_surface_set_need_update(surface, true);
	}
	return true;
}

static void handle_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	UNUSED(pointer);
	if (!surface) {
		return;
	}
	struct nwl_seat *seat = data;
	struct nwl_surface *nwlsurf = wl_surface_get_user_data(surface);
	seat->pointer_focus = nwlsurf;
	seat->pointer_event->surface_x = surface_x;
	seat->pointer_event->surface_y = surface_y;
	seat->pointer_event->focus = true;
	seat->pointer_event->serial = serial;
	seat->pointer_event->changed |= NWL_POINTER_EVENT_MOTION | NWL_POINTER_EVENT_FOCUS;
	// Clear button state if entering the same surface that was last focused.
	// This so there isn't stale button state if focus was lost because of an interactive drag, for example.
	if (seat->pointer_focus == seat->pointer_prev_focus) {
		seat->pointer_event->buttons = 0;
	}
	if (!(nwlsurf->flags & NWL_SURFACE_FLAG_NO_AUTOCURSOR)) {
		nwl_seat_set_pointer_shape(seat, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
	}
}

static void handle_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
	UNUSED(pointer);
	UNUSED(surface);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (seat->pointer_focus) {
		seat->pointer_event->serial = serial;
		seat->pointer_event->focus = false;
		seat->pointer_event->changed |= NWL_POINTER_EVENT_FOCUS;
	}
}

static void handle_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	seat->pointer_event->changed |= NWL_POINTER_EVENT_MOTION;
	seat->pointer_event->surface_x = surface_x;
	seat->pointer_event->surface_y = surface_y;
}

static char map_linuxmbutton_to_nwl(uint32_t button) {
	switch (button) {
		case BTN_LEFT:
			return NWL_MOUSE_LEFT;
		case BTN_RIGHT:
			return NWL_MOUSE_RIGHT;
		case BTN_MIDDLE:
			return NWL_MOUSE_MIDDLE;
		case BTN_EXTRA:
			return NWL_MOUSE_FORWARD;
		case BTN_SIDE:
			return NWL_MOUSE_BACK;
		default:
			fprintf(stderr, "Unknown mouse button %x\n", button);
			return 0;
	}
}

static void handle_pointer_button(void *data,
		struct wl_pointer *wl_pointer,
		uint32_t serial,
		uint32_t time,
		uint32_t button,
		uint32_t state) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	char nwl_mbutton = map_linuxmbutton_to_nwl(button);
	seat->pointer_event->changed |= NWL_POINTER_EVENT_BUTTON;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		seat->pointer_event->buttons |= nwl_mbutton;
	} else {
		char cur = seat->pointer_event->buttons;
		seat->pointer_event->buttons = cur & ~nwl_mbutton;
	}
	seat->pointer_event->serial = serial;
}

static void handle_pointer_axis(void *data,
	struct wl_pointer *wl_pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = data;
	if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
		seat->pointer_event->axis_hori += value;
	} else {
		seat->pointer_event->axis_vert += value;
	}
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}

static void handle_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	struct nwl_seat *seat = data;
	UNUSED(wl_pointer);
	if (seat->pointer_focus && seat->pointer_focus->impl.input_pointer) {
		// Hmm.. how does this behave if the pointer moves across two surfaces in the same frame?
		seat->pointer_focus->impl.input_pointer(seat->pointer_focus, seat, seat->pointer_event);
	}
	if (seat->pointer_event->changed & NWL_POINTER_EVENT_FOCUS && !seat->pointer_event->focus) {
		seat->pointer_prev_focus = seat->pointer_focus;
		seat->pointer_focus = NULL;
	}
	if (seat->pointer_event->changed & NWL_POINTER_EVENT_BUTTON) {
		seat->pointer_event->buttons_prev = seat->pointer_event->buttons;
	}
	seat->pointer_event->changed = 0;
	seat->pointer_event->axis_value_hori = 0;
	seat->pointer_event->axis_value_vert = 0;
	seat->pointer_event->axis_hori = 0;
	seat->pointer_event->axis_vert = 0;
	seat->pointer_event->axis_source = 0;
}

static char wl_axis_source_to_nwl(uint32_t axis_source) {
	switch (axis_source) {
		case WL_POINTER_AXIS_SOURCE_WHEEL:
			return NWL_AXIS_SOURCE_WHEEL;
		case WL_POINTER_AXIS_SOURCE_CONTINUOUS:
			return NWL_AXIS_SOURCE_CONTINUOUS;
		case WL_POINTER_AXIS_SOURCE_FINGER:
			return NWL_AXIS_SOURCE_FINGER;
		case WL_POINTER_AXIS_SOURCE_WHEEL_TILT:
			return NWL_AXIS_SOURCE_WHEEL_TILT;
		default:
			return 0;
	}
}

static char wl_axis_to_nwl(uint32_t axis) {
	switch(axis) {
		case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
			return NWL_AXIS_HORIZONTAL;
		case WL_POINTER_AXIS_VERTICAL_SCROLL:
			return NWL_AXIS_VERTICAL;
		default:
			return 0;
	}
}

static void handle_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
	UNUSED(wl_pointer);
	struct nwl_seat *seat = data;
	seat->pointer_event->axis_source |= wl_axis_source_to_nwl(axis_source);
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}

static void handle_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		uint32_t axis) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = data;
	seat->pointer_event->axis_stop |= wl_axis_to_nwl(axis);
}

static void handle_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis,
		int32_t discrete) {
	UNUSED(wl_pointer);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		seat->pointer_event->axis_value_vert += discrete * 120;
	} else {
		seat->pointer_event->axis_value_hori += discrete * 120;
	}
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}

static void handle_pointer_axis_value120(void *data, struct wl_pointer *wl_pointer, uint32_t axis,
		int32_t value120) {
	UNUSED(wl_pointer);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		seat->pointer_event->axis_value_vert += value120;
	} else {
		seat->pointer_event->axis_value_hori += value120;
	}
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}

static const struct wl_pointer_listener pointer_listener = {
	handle_pointer_enter,
	handle_pointer_leave,
	handle_pointer_motion,
	handle_pointer_button,
	handle_pointer_axis,
	handle_pointer_frame,
	handle_pointer_axis_source,
	handle_pointer_axis_stop,
	handle_pointer_axis_discrete,
	handle_pointer_axis_value120
};
/*
static void handle_touch_down(void *data,
		struct wl_touch *wl_touch,
		uint32_t serial,
		uint32_t time,
		struct wl_surface *surface,
		int32_t id,
		wl_fixed_t x,
		wl_fixed_t y) {
	// bla
}

static void handle_touch_up(void *data,
		struct wl_touch *wl_touch,
		uint32_t serial,
		uint32_t time,
		int32_t id) {
	// bla
}

static void handle_touch_motion(void *data,
		struct wl_touch *wl_touch,
		uint32_t time,
		int32_t id,
		wl_fixed_t x,
		wl_fixed_t y) {
	// bla
}

static void handle_touch_frame(void *data,
		struct wl_touch *wl_touch) {
	// bla
}

static void handle_touch_cancel(void *data,
		struct wl_touch *wl_touch) {
	// bla
}

static void handle_touch_shape(void *data,
		struct wl_touch *wl_touch,
		int32_t id,
		wl_fixed_t major,
		wl_fixed_t minor) {
	// bla
}

static void handle_touch_orientation(void *data,
		struct wl_touch *wl_touch,
		int32_t id,
		wl_fixed_t orientation) {
	// bla
}

static const struct wl_touch_listener touch_listener = {
	handle_touch_down,
	handle_touch_up,
	handle_touch_motion,
	handle_touch_frame,
	handle_touch_cancel,
	handle_touch_shape,
	handle_touch_orientation
};
*/

static void seat_release_keyboard(struct nwl_seat *seat) {
	free(seat->keyboard_event);
	wl_keyboard_release(seat->keyboard);
		unref_seat_xkb(seat);
	seat->keyboard = NULL;
}

static void seat_release_pointer(struct nwl_seat *seat) {
	free(seat->pointer_event);
	wl_pointer_release(seat->pointer);
	if (seat->pointer_surface.xcursor_surface) {
		wl_surface_destroy(seat->pointer_surface.xcursor_surface);
		seat->pointer_surface.xcursor_surface = NULL;
	}
	if (seat->pointer_surface.shape_device) {
		wp_cursor_shape_device_v1_destroy(seat->pointer_surface.shape_device);
		seat->pointer_surface.shape_device = NULL;
	}
	seat->pointer = NULL;
}

static void handle_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
	struct nwl_seat *nwseat = data;
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		if (!nwseat->keyboard) {
			nwseat->keyboard = wl_seat_get_keyboard(seat);
			nwseat->keyboard_event = calloc(1, sizeof(struct nwl_keyboard_event));
			wl_keyboard_add_listener(nwseat->keyboard, &keyboard_listener, data);
		}
	} else if (nwseat->keyboard) {
		seat_release_keyboard(nwseat);
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		if (!nwseat->pointer) {
			nwseat->pointer = wl_seat_get_pointer(seat);
			nwseat->pointer_event = calloc(1, sizeof(struct nwl_pointer_event));
			wl_pointer_add_listener(nwseat->pointer, &pointer_listener, data);
		}
	} else if (nwseat->pointer) {
		seat_release_pointer(nwseat);
	}
	/*
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		nwseat->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(nwseat->touch, &touch_listener, data);
	}
	*/
}

static void handle_seat_name(void *data, struct wl_seat *seat, const char *name) {
	UNUSED(seat);
	struct nwl_seat *nwseat = (struct nwl_seat*)data;
	nwseat->name = strdup(name);
}

static const struct wl_seat_listener seat_listener = {
	handle_seat_capabilities,
	handle_seat_name
};

static void handle_data_offer_offer(void *data, struct wl_data_offer *wl_data_offer, const char *mime_type) {
	struct nwl_seat *seat = data;
	if (seat->data_device.incoming.offer != wl_data_offer) {
		return; // eek!
	}
	char **dest = wl_array_add(&seat->data_device.incoming.mime, sizeof(char*));
	*dest = strdup(mime_type);
}

static void handle_data_offer_source_actions(void *data, struct wl_data_offer *wl_data_offer,
		uint32_t source_actions) {
	struct nwl_seat *seat = data;
	if (wl_data_offer != seat->data_device.drop.offer) {
		return;
	}
	seat->data_device.event.source_actions = source_actions;
}

static void handle_data_offer_action(void *data, struct wl_data_offer *wl_data_offer,
		uint32_t dnd_action) {
	struct nwl_seat *seat = data;
	if (wl_data_offer != seat->data_device.drop.offer) {
		return;
	}
	seat->data_device.event.action = dnd_action;
}

static const struct wl_data_offer_listener data_offer_listener = {
	handle_data_offer_offer,
	handle_data_offer_source_actions,
	handle_data_offer_action
};

static void data_device_offer(void *data, struct wl_data_device *wl_data_device,
		struct wl_data_offer *id) {
	UNUSED(wl_data_device);
	struct nwl_seat *seat = data;
	if (seat->data_device.incoming.offer) {
		// eek! Don't know whether the previous one is drop or selection!
		return;
	}
	seat->data_device.incoming.offer = id;
	wl_data_offer_add_listener(id, &data_offer_listener, seat);
	wl_array_init(&seat->data_device.incoming.mime);
}

static inline void move_offer(struct nwl_data_offer *dest, struct nwl_data_offer *src) {
	memcpy(dest, src, sizeof(struct nwl_data_offer));
	memset(src, 0, sizeof(struct nwl_data_offer));
}

static inline void destroy_offer(struct nwl_data_offer *offer) {
	char **mimetype;
	wl_array_for_each(mimetype, &offer->mime) {
		free(*mimetype);
	}
	wl_array_release(&offer->mime);
	wl_data_offer_destroy(offer->offer);
	offer->offer = 0;
}

static void data_device_enter(void *data, struct wl_data_device *wl_data_device,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *id) {
	UNUSED(wl_data_device);
	struct nwl_seat *seat = data;
	if (id) {
		if (seat->data_device.incoming.offer != id) {
			// eek! Don't know what happened here!
			return;
		}
		if (seat->data_device.drop.offer) {
			// Shouldn't happen!
			destroy_offer(&seat->data_device.drop);
		}
		move_offer(&seat->data_device.drop, &seat->data_device.incoming);
	}
	struct nwl_surface *surf = wl_surface_get_user_data(surface);
	if (surf->impl.dnd) {
		seat->data_device.event.x = x;
		seat->data_device.event.y = y;
		seat->data_device.event.type = NWL_DND_EVENT_ENTER;
		seat->data_device.event.serial = serial;
		seat->data_device.event.focus_surface = surf;
		surf->impl.dnd(surf, seat, &seat->data_device.event);
	}
}

static void data_device_leave(void *data, struct wl_data_device *wl_data_device) {
	UNUSED(wl_data_device);
	struct nwl_seat *seat = data;
	if (seat->data_device.event.focus_surface && seat->data_device.event.focus_surface->impl.dnd) {
		seat->data_device.event.type = NWL_DND_EVENT_LEFT;
		seat->data_device.event.focus_surface->impl.dnd(seat->data_device.event.focus_surface, seat,
			&seat->data_device.event);
	}
	if (seat->data_device.drop.offer) {
		destroy_offer(&seat->data_device.drop);
	}
}

static void data_device_motion(void *data, struct wl_data_device *wl_data_device,
		uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	UNUSED(wl_data_device);

	struct nwl_seat *seat = data;
	if (seat->data_device.event.focus_surface && seat->data_device.event.focus_surface->impl.dnd) {
		seat->data_device.event.x = x;
		seat->data_device.event.y = y;
		seat->data_device.event.time = time;
		seat->data_device.event.type = NWL_DND_EVENT_MOTION;
		seat->data_device.event.focus_surface->impl.dnd(seat->data_device.event.focus_surface, seat,
				&seat->data_device.event);
	}
}

static void data_device_drop(void *data, struct wl_data_device *wl_data_device) {
	UNUSED(wl_data_device);
	struct nwl_seat *seat = data;
	if (seat->data_device.event.focus_surface && seat->data_device.event.focus_surface->impl.dnd) {
		seat->data_device.event.type = NWL_DND_EVENT_DROP;
		seat->data_device.event.focus_surface->impl.dnd(seat->data_device.event.focus_surface, seat,
				&seat->data_device.event);
	}
}

static void data_device_selection(void *data, struct wl_data_device *wl_data_device, struct wl_data_offer *id) {
	UNUSED(wl_data_device);
	struct nwl_seat *seat = data;
	if (id != seat->data_device.incoming.offer) {
		// eek!
		return;
	}
	if (seat->data_device.selection.offer) {
		destroy_offer(&seat->data_device.selection);
	}
	move_offer(&seat->data_device.selection, &seat->data_device.incoming);
}

static const struct wl_data_device_listener data_device_listener = {
	data_device_offer,
	data_device_enter,
	data_device_leave,
	data_device_motion,
	data_device_drop,
	data_device_selection
};

void nwl_seat_clear_focus(struct nwl_surface *surface) {
	struct nwl_seat *seat;
	struct nwl_core *core = surface->core;
	wl_list_for_each(seat, &core->seats, link) {
		if (seat->pointer_focus == surface) {
			seat->pointer_focus = NULL;
			seat->pointer_event->buttons = 0;
			seat->pointer_event->buttons_prev = 0;
		}
		if (seat->pointer_prev_focus == surface) {
			seat->pointer_prev_focus = NULL;
		}
		if (seat->keyboard_focus == surface) {
			seat->keyboard_focus = NULL;
		}
	}
}

void nwl_seat_deinit(struct nwl_seat *seat) {
	wl_list_remove(&seat->link);
	if (seat->pointer) {
		seat_release_pointer(seat);
	}
	if (seat->keyboard) {
		seat_release_keyboard(seat);
	}
	if (seat->name) {
		free(seat->name);
	}
	if (seat->data_device.wl) {
		if (seat->data_device.drop.offer) {
			destroy_offer(&seat->data_device.drop);
		}
		if (seat->data_device.incoming.offer) {
			destroy_offer(&seat->data_device.incoming);
		}
		if (seat->data_device.selection.offer) {
			destroy_offer(&seat->data_device.selection);
		}
		wl_data_device_release(seat->data_device.wl);
	}
	wl_seat_release(seat->wl_seat);
}

void nwl_seat_add_data_device(struct nwl_seat *seat) {
	seat->data_device.wl = wl_data_device_manager_get_data_device(seat->core->wl.data_device_manager, seat->wl_seat);
	wl_data_device_add_listener(seat->data_device.wl, &data_device_listener, seat);
}

void nwl_seat_init(struct nwl_seat *nwlseat, struct wl_seat *wlseat, struct nwl_core *core) {
	// Hm, not really liking this..
	memset(nwlseat, 0, sizeof(struct nwl_seat));
	nwlseat->core = core;
	nwlseat->wl_seat = wlseat;
	nwlseat->keyboard_repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

	wl_list_insert(&core->seats, &nwlseat->link);
	wl_seat_add_listener(wlseat, &seat_listener, nwlseat);
	if (core->wl.data_device_manager) {
		nwl_seat_add_data_device(nwlseat);
	}

}

bool nwl_seat_start_drag(struct nwl_seat *seat, struct wl_data_source *wl_data_source, struct nwl_surface *icon) {
	if (!seat->pointer_focus || !seat->data_device.wl) {
		return false;
	}
	// Allow drag origin surface that's not current focus?
	// Also need to support touch drag..
	if (icon) {
		if (icon->role_id && icon->role_id != NWL_SURFACE_ROLE_DRAGICON) {
			return false;
		}
		resize_surface_to_desired(icon, seat->pointer_focus->scale);
		if (!icon->role_id) {
			icon->role_id = NWL_SURFACE_ROLE_DRAGICON;
		}
	}
	seat->pointer_event->buttons = 0;
	seat->pointer_event->buttons_prev = 0;
	wl_data_device_start_drag(seat->data_device.wl, wl_data_source, seat->pointer_focus->wl.surface,
			icon ? icon->wl.surface: NULL, seat->pointer_event->serial);
	if (icon) {
		nwl_surface_set_need_update(icon, true);
	}
	return true;
}
