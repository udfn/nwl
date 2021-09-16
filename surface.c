#define _POSIX_C_SOURCE 200112L
#include <wayland-client.h>
#include <wayland-egl.h>
#include <epoxy/egl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "viewporter.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

// Is in wayland.c
void surface_mark_dirty(struct nwl_surface *surface);

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

static int create_shm_file(void) {
	int retries = 100;
	do {
		char name[] = "/nwl_shm-XXXXXX";
		randname(name + sizeof(name) - 7);
		--retries;
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);
	return -1;
}

static int allocate_shm_file(size_t size) {
	int fd = create_shm_file();
	if (fd < 0)
		return -1;
	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void destroy_shm_pool(struct nwl_surface_shm *shm) {
	if (shm->fd) {
		wl_shm_pool_destroy(shm->pool);
		munmap(shm->data, shm->size);
		close(shm->fd);
	}
}

static void allocate_wl_shm_pool(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	int stride = surface->renderer.impl->get_stride(WL_SHM_FORMAT_ARGB8888, scaled_width);
	shm->stride = stride;
	int pool_size = scaled_height * stride * 2;
	if (shm->fd) {
		destroy_shm_pool(shm);
	}
	int fd = allocate_shm_file(pool_size);
	shm->fd = fd;
	shm->data = mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	shm->pool = wl_shm_create_pool(surface->state->wl.shm, fd, pool_size);
	surface->renderer.impl->surface_create(surface, NWL_SURFACE_RENDER_SHM, scaled_width, scaled_height);
	return;
}

static void shm_surface_destroy(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render.data;
	if (surface->renderer.impl) {
		surface->renderer.impl->surface_destroy(surface, NWL_SURFACE_RENDER_SHM);
		destroy_shm_pool(shm);
	}
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	free(surface->render.data);
}

static void shm_surface_swapbuffers(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render.data;
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	uint32_t scaled_width = surface->width*surface->scale;
	uint32_t scaled_height = surface->height*surface->scale;
	shm->buffer = wl_shm_pool_create_buffer(shm->pool, 0, scaled_width, scaled_height, shm->stride, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface->wl.surface, shm->buffer, 0,0);
	wl_surface_commit(surface->wl.surface);
}

static void surface_render_set_shm(struct nwl_surface *surface) {
	if (surface->render.data) {
		surface->render.impl.destroy(surface);
	}
	surface->render.data = calloc(sizeof(struct nwl_surface_shm),1);
	surface->render.impl.destroy = shm_surface_destroy;
	surface->render.impl.applysize = allocate_wl_shm_pool;
	surface->render.impl.swapbuffers = shm_surface_swapbuffers;
}

static void egl_surface_destroy(struct nwl_surface *surface) {
	struct nwl_surface_egl *egl = surface->render.data;
	if (surface->renderer.impl) {
		surface->renderer.impl->surface_destroy(surface, NWL_SURFACE_RENDER_EGL);
		wl_egl_window_destroy(egl->window);
		eglDestroySurface(surface->state->egl.display, egl->surface);
	}
	free(surface->render.data);
}

static void egl_surface_applysize(struct nwl_surface *surface) {
	struct nwl_surface_egl *egl = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	if (!egl->window) {
		egl->window = wl_egl_window_create(surface->wl.surface, scaled_width, scaled_height);
		egl->surface = eglCreatePlatformWindowSurfaceEXT(surface->state->egl.display, surface->state->egl.config, egl->window, NULL);
		surface->renderer.impl->surface_create(surface, NWL_SURFACE_RENDER_EGL, scaled_width, scaled_height);
	} else {
		wl_egl_window_resize(egl->window, scaled_width, scaled_height, 0, 0);
		surface->renderer.impl->surface_set_size(surface, scaled_width, scaled_height);
	}
}

static void egl_surface_swapbuffers(struct nwl_surface *surface) {
	surface->renderer.impl->swap_buffers(surface);
}

static void surface_render_set_egl(struct nwl_surface *surface) {
	if (surface->render.data) {
		surface->render.impl.destroy(surface);
	}
	if (surface->state->egl.inited == 2) {
		surface_render_set_shm(surface);
		return;
	} else if (surface->state->egl.inited == 0) {
		if (!nwl_egl_try_init(surface->state)) {
			surface_render_set_shm(surface);
			return;
		}
	}
	surface->render.data = calloc(sizeof(struct nwl_surface_egl), 1);
	surface->render.impl.destroy = egl_surface_destroy;
	surface->render.impl.applysize = egl_surface_applysize;
	surface->render.impl.swapbuffers = egl_surface_swapbuffers;
}

struct wl_callback_listener callback_listener;

static void nwl_surface_real_apply_size(struct nwl_surface *surface) {
	surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	surface->render.impl.applysize(surface);
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
}

void nwl_surface_render(struct nwl_surface *surface) {
	surface->rendering = true;
	surface->states = surface->states & ~NWL_SURFACE_STATE_NEEDS_DRAW;
	if (surface->states & NWL_SURFACE_STATE_NEEDS_APPLY_SIZE) {
		nwl_surface_real_apply_size(surface);
	}
	surface->renderer.impl->render(surface);
	surface->rendering = false;
}

static void cb_done(void *data, struct wl_callback *cb, uint32_t cb_data) {
	UNUSED(cb_data);
	struct nwl_surface *surf = data;
	surf->wl.frame_cb = NULL;
	wl_callback_destroy(cb);
	if (surf->states & NWL_SURFACE_STATE_NEEDS_DRAW) {
		nwl_surface_render(surf);
	}
}

struct wl_callback_listener callback_listener = {
	cb_done
};


static void surface_set_scale_from_outputs(struct nwl_surface *surf) {
	int scale = 1;
	struct nwl_surface_output *surfoutput;
	wl_list_for_each(surfoutput, &surf->outputs, link) {
		struct nwl_output *nwoutput = wl_output_get_user_data(surfoutput->output);
		if (nwoutput && nwoutput->scale > scale) {
			scale = nwoutput->scale;
		}
	}
	if (scale != surf->scale) {
		surf->scale = scale;
		surf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		nwl_surface_set_need_draw(surf, true);
	}
}

static void handle_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	struct nwl_surface_output *surfoutput = calloc(1, sizeof(struct nwl_surface_output));
	surfoutput->output = output;
	wl_list_insert(&surf->outputs, &surfoutput->link);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}

static void handle_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct nwl_surface *surf = data;
	struct nwl_surface_output *surfoutput;
	wl_list_for_each(surfoutput, &surf->outputs, link) {
		if (surfoutput->output == output) {
			break;
		}
	}
	wl_list_remove(&surfoutput->link);
	free(surfoutput);
	if (!(surf->flags & NWL_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}
static const struct wl_surface_listener surface_listener = {
	handle_surface_enter,
	handle_surface_leave
};

struct nwl_surface *nwl_surface_create(struct nwl_state *state, char *title, enum nwl_surface_renderer renderer) {
	struct nwl_surface *newsurf = calloc(1, sizeof(struct nwl_surface));
	newsurf->state = state;
	newsurf->wl.surface = wl_compositor_create_surface(state->wl.compositor);
	newsurf->scale = 1;
	newsurf->title = title;
	wl_list_init(&newsurf->subsurfaces);
	wl_list_init(&newsurf->outputs);
	wl_list_init(&newsurf->dirtlink);
	wl_list_insert(&state->surfaces, &newsurf->link);
	wl_surface_set_user_data(newsurf->wl.surface, newsurf);
	wl_surface_add_listener(newsurf->wl.surface, &surface_listener, newsurf);
	if (renderer == NWL_SURFACE_RENDER_SHM) {
		surface_render_set_shm(newsurf);
	} else {
		surface_render_set_egl(newsurf);
	}
	return newsurf;
}

void nwl_surface_destroy(struct nwl_surface *surface) {
	// clear any focuses
	nwl_seat_clear_focus(surface);
	// Probably not necessary but it doesn't hurt to be sure
	if (surface->wl.frame_cb) {
		wl_callback_destroy(surface->wl.frame_cb);
	}
	if (!wl_list_empty(&surface->dirtlink)) {
		wl_list_remove(&surface->dirtlink);
	}
	wl_list_remove(&surface->link);
	if (surface->impl.destroy) {
		surface->impl.destroy(surface);
	}
	surface->render.impl.destroy(surface);
	struct nwl_surface_output *surfoutput, *surfoutputtmp;
	wl_list_for_each_safe(surfoutput, surfoutputtmp, &surface->outputs, link) {
		wl_list_remove(&surfoutput->link);
		free(surfoutput);
	}
	if (surface->wl.xdg_surface) {
		if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL) {
			if (surface->wl.xdg_decoration) {
				zxdg_toplevel_decoration_v1_destroy(surface->wl.xdg_decoration);
			}
			xdg_toplevel_destroy(surface->role.toplevel.wl);
		} else if (surface->role_id == NWL_SURFACE_ROLE_POPUP) {
			xdg_popup_destroy(surface->role.popup.wl);
		}
		xdg_surface_destroy(surface->wl.xdg_surface);
	} else if (surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		zwlr_layer_surface_v1_destroy(surface->role.layer.wl);
	} else if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
		wl_subsurface_destroy(surface->role.subsurface.wl);
	}
	wl_surface_destroy(surface->wl.surface);
	if (surface->role_id == NWL_SURFACE_ROLE_TOPLEVEL ||
			surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		surface->state->num_surfaces--;
	}
	// And finally, destroy subsurfaces!
	struct nwl_surface *subsurf, *subsurftmp;
	wl_list_for_each_safe(subsurf, subsurftmp, &surface->subsurfaces, link) {
		nwl_surface_destroy(subsurf);
	}
	free(surface);
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

bool nwl_surface_set_vp_destination(struct nwl_surface *surface, int32_t width, int32_t height) {
	if (!surface->state->wl.viewporter) {
		return false;
	}
	if (!surface->wl.viewport) {
		surface->wl.viewport = wp_viewporter_get_viewport(surface->state->wl.viewporter, surface->wl.surface);
	}
	wp_viewport_set_destination(surface->wl.viewport, width, height);
	if (width == -1 && height == -1) {
		surface->actual_height = surface->height;
		surface->actual_width = surface->width;
	} else {
		surface->actual_height = height;
		surface->actual_width = width;
	}
	return true;
}

void nwl_surface_set_size(struct nwl_surface *surface, uint32_t width, uint32_t height) {
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
	surface->desired_height = height;
	surface->desired_width = width;
	if (surface->role_id == NWL_SURFACE_ROLE_LAYER) {
		zwlr_layer_surface_v1_set_size(surface->role.layer.wl, width, height);
	} else if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
		surface->width = width;
		surface->height = height;
	}
}

void nwl_surface_swapbuffers(struct nwl_surface *surface) {
	surface->frame++;
	if (!surface->wl.frame_cb) {
		surface->wl.frame_cb = wl_surface_frame(surface->wl.surface);
		wl_callback_add_listener(surface->wl.frame_cb, &callback_listener, surface);
	}
	uint32_t scaled_width, scaled_height;
	scaled_width = surface->width*surface->scale;
	scaled_height = surface->height*surface->scale;
	wl_surface_damage_buffer(surface->wl.surface, 0, 0, scaled_width, scaled_height);
	surface->render.impl.swapbuffers(surface);
}

void nwl_surface_set_need_draw(struct nwl_surface *surface, bool render) {
	surface->states |= NWL_SURFACE_STATE_NEEDS_DRAW;
	if (surface->wl.frame_cb || surface->rendering) {
		return;
	}
	if (render) {
		nwl_surface_render(surface);
		return;
	}
	surface_mark_dirty(surface);
}

bool nwl_surface_role_subsurface(struct nwl_surface *surface, struct nwl_surface *parent) {
	if (surface->role_id) {
		return false;
	}
	surface->role.subsurface.wl = wl_subcompositor_get_subsurface(surface->state->wl.subcompositor,
			surface->wl.surface, parent->wl.surface);
	surface->parent = parent;
	wl_list_remove(&surface->link);
	wl_list_insert(&parent->subsurfaces, &surface->link);
	surface->role_id = NWL_SURFACE_ROLE_SUB;
	return true;
}
