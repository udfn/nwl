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


void nwl_shm_destroy_pool(struct nwl_surface_shm *shm) {
	if (shm->fd) {
		wl_shm_pool_destroy(shm->pool);
		munmap(shm->data, shm->size);
		close(shm->fd);
		shm->fd = 0;
	}
}

void nwl_shm_set_size(struct nwl_surface_shm *shm, struct nwl_state *state, size_t pool_size) {
	if (shm->size != pool_size) {
		nwl_shm_destroy_pool(shm);
		int fd = nwl_allocate_shm_file(pool_size);
		shm->fd = fd;
		shm->data = mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
		shm->pool = wl_shm_create_pool(state->wl.shm, fd, pool_size);
		shm->size = pool_size;
	}
}

struct wl_buffer *nwl_shm_get_buffer(struct nwl_surface_shm *shm, uint32_t width,
		uint32_t height, uint32_t stride, uint32_t format) {
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	shm->buffer = wl_shm_pool_create_buffer(shm->pool, 0, width, height, stride, format);
	return shm->buffer;
}

void nwl_shm_destroy(struct nwl_surface_shm *shm) {
	nwl_shm_destroy_pool(shm);
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	free(shm);
}

struct nwl_surface_shm *nwl_shm_create() {
	// Why be like this?
	return calloc(1, sizeof(struct nwl_surface_shm));
}
