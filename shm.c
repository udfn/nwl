#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <wayland-client.h>
#include "nwl/shm.h"
#include "nwl/surface.h"
#include "nwl/nwl.h"

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

int nwl_allocate_shm_file(size_t size) {
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
		shm->fd = 0;
	}
}

static void shm_applysize(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render_backend.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	int stride = surface->render.impl->get_stride(WL_SHM_FORMAT_ARGB8888, scaled_width);
	shm->stride = stride;
	size_t pool_size = scaled_height * stride * 2;
	if (shm->size != pool_size) {
		destroy_shm_pool(shm);
		int fd = nwl_allocate_shm_file(pool_size);
		shm->fd = fd;
		shm->data = mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		shm->pool = wl_shm_create_pool(surface->state->wl.shm, fd, pool_size);
		shm->size = pool_size;
	}
	surface->render.impl->surface_create(surface, scaled_width, scaled_height);
	return;
}

static void shm_destroy(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render_backend.data;
	if (surface->render.impl) {
		destroy_shm_pool(shm);
	}
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	free(surface->render_backend.data);
}

static void shm_swapbuffers(struct nwl_surface *surface) {
	struct nwl_surface_shm *shm = surface->render_backend.data;
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	uint32_t scaled_width = surface->width*surface->scale;
	uint32_t scaled_height = surface->height*surface->scale;
	shm->buffer = wl_shm_pool_create_buffer(shm->pool, 0, scaled_width, scaled_height, shm->stride, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface->wl.surface, shm->buffer, 0, 0);
	wl_surface_commit(surface->wl.surface);
}

static void shm_destroy_surface(struct nwl_surface *surface) {
	surface->render.impl->surface_destroy(surface);
}

static struct nwl_render_backend_impl shm_backend = {
	shm_swapbuffers,
	shm_applysize,
	shm_destroy,
	shm_destroy_surface
};

void nwl_surface_render_backend_shm(struct nwl_surface *surface) {
	surface->render_backend.data = calloc(sizeof(struct nwl_surface_shm), 1);
	surface->render_backend.impl = &shm_backend;
	surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
}
