#ifndef _NWL_SEAT_H_
#define _NWL_SEAT_H_

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>

typedef uint32_t xkb_keysym_t;
typedef uint32_t xkb_keycode_t;

struct wl_data_source;

struct nwl_data_offer {
	struct wl_array mime;
	struct wl_data_offer *offer;
};

enum nwl_dnd_event_type {
	NWL_DND_EVENT_MOTION,
	NWL_DND_EVENT_ENTER,
	NWL_DND_EVENT_LEFT,
	NWL_DND_EVENT_DROP,
};

struct nwl_dnd_event {
	char type; // enum nwl_dnd_event_type
	uint32_t serial;
	struct nwl_surface *focus_surface;
	wl_fixed_t x;
	wl_fixed_t y;
	uint32_t time;
	uint32_t source_actions;
	uint32_t action; // chosen by compositor
};

struct nwl_seat {
	struct nwl_state *state;
	struct wl_list link;
	struct wl_seat *wl_seat;
	struct {
		struct wl_data_device *wl;
		struct nwl_data_offer drop;
		struct nwl_data_offer selection;
		struct nwl_data_offer incoming; // before it says whether it's dnd or selection
		struct nwl_dnd_event event;
	} data_device;

	struct wl_keyboard *keyboard;
	struct xkb_keymap *keyboard_keymap;
	struct xkb_state *keyboard_state;
	bool keyboard_compose_enabled; // Recommended when typing! Maybe move this to surface?
	bool keyboard_repeat_enabled;
	struct xkb_compose_state *keyboard_compose_state;
	struct xkb_compose_table *keyboard_compose_table;
	struct nwl_surface *keyboard_focus;
	int32_t keyboard_repeat_rate;
	int32_t keyboard_repeat_delay;
	int keyboard_repeat_fd;
	struct nwl_keyboard_event *keyboard_event;

	struct wl_touch *touch;
	struct nwl_surface *touch_focus;
	uint32_t touch_serial;

	struct wl_pointer *pointer;
	struct nwl_surface *pointer_focus;
	struct {
		struct wl_cursor *xcursor;
		struct wl_surface *xcursor_surface;
		struct nwl_surface *nwl;
		int32_t hot_x;
		int32_t hot_y;
	} pointer_surface;
	struct nwl_pointer_event *pointer_event;
	char *name;
};

enum nwl_pointer_event_changed {
	NWL_POINTER_EVENT_FOCUS = 1 << 0,
	NWL_POINTER_EVENT_BUTTON = 1 << 1,
	NWL_POINTER_EVENT_MOTION = 1 << 2,
	NWL_POINTER_EVENT_AXIS = 1 << 3,
};

enum nwl_pointer_buttons {
	NWL_MOUSE_LEFT = 1 << 0,
	NWL_MOUSE_MIDDLE = 1 << 1,
	NWL_MOUSE_RIGHT = 1 << 2,
	NWL_MOUSE_BACK = 1 << 3,
	NWL_MOUSE_FORWARD = 1 << 4,
};

// basically wl_pointer_axis_source converted to bitflags
enum nwl_pointer_axis_source {
	NWL_AXIS_SOURCE_WHEEL = 1 << 0,
	NWL_AXIS_SOURCE_FINGER = 1 << 1,
	NWL_AXIS_SOURCE_CONTINUOUS = 1 << 2,
	NWL_AXIS_SOURCE_WHEEL_TILT = 1 << 3,
};

enum nwl_pointer_axis {
	NWL_AXIS_VERTICAL = 1 << 0,
	NWL_AXIS_HORIZONTAL = 1 << 1,
};

struct nwl_pointer_event {
	char changed; // nwl_seat_pointer_event_changed
	uint32_t serial;
	wl_fixed_t surface_x;
	wl_fixed_t surface_y;
	char buttons; // nwl_pointer_buttons
	char buttons_prev;
	int32_t axis_discrete_vert;
	int32_t axis_discrete_hori;
	wl_fixed_t axis_hori;
	wl_fixed_t axis_vert;
	char axis_source; // nwl_pointer_axis_source
	char axis_stop; // nwl_pointer_axis
	bool focus;
};

enum nwl_keyboard_event_type {
	NWL_KEYBOARD_EVENT_FOCUS,
	NWL_KEYBOARD_EVENT_KEYDOWN,
	NWL_KEYBOARD_EVENT_KEYUP,
	NWL_KEYBOARD_EVENT_KEYREPEAT,
	NWL_KEYBOARD_EVENT_MODIFIERS,
};

enum nwl_keyboard_compose_state {
	NWL_KEYBOARD_COMPOSE_NONE,
	NWL_KEYBOARD_COMPOSE_COMPOSING,
	NWL_KEYBOARD_COMPOSE_COMPOSED
};

struct nwl_keyboard_event {
	char type; // nwl_keyboard_event_type
	char compose_state; // nwl_keyboard_compose_state
	bool focus;
	// If the compose state is composed then keysym and utf8 is the composed sym!
	xkb_keysym_t keysym;
	xkb_keycode_t keycode;
	char utf8[16];
	uint32_t serial;
};

void nwl_seat_create(struct wl_seat *wlseat, struct nwl_state *state, uint32_t name);
void nwl_seat_clear_focus(struct nwl_surface *surface);
// Doesn't work right when using a nwl_surface for the pointer cursor
void nwl_seat_get_pointer_cursor_metrics(struct nwl_seat *seat, int32_t *width, int32_t *height, int32_t *hotspot_x, int32_t *hotspot_y);
void nwl_seat_set_pointer_cursor(struct nwl_seat *seat, const char *cursor);
bool nwl_seat_set_pointer_surface(struct nwl_seat *seat, struct nwl_surface *surface, int32_t hotspot_x, int32_t hotspot_y);
bool nwl_seat_start_drag(struct nwl_seat *seat, struct wl_data_source *data_source, struct nwl_surface *icon);

#endif
