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

void nwl_shm_destroy_pool(struct nwl_shm_pool *shm) {
	if (shm->fd) {
		wl_shm_pool_destroy(shm->pool);
		munmap(shm->data, shm->size);
		close(shm->fd);
		shm->fd = 0;
	}
}

void nwl_shm_set_size(struct nwl_shm_pool *shm, struct nwl_state *state, size_t pool_size) {
	if (shm->size != pool_size) {
		if (!shm->fd) {
			shm->fd = nwl_allocate_shm_file(pool_size);
		} else {
			wl_shm_pool_destroy(shm->pool);
			munmap(shm->data, shm->size);
			ftruncate(shm->fd, pool_size);
		}
		shm->data = mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, shm->fd, 0);
		shm->pool = wl_shm_create_pool(state->wl.shm, shm->fd, pool_size);
		shm->size = pool_size;
	}
}

static void handle_buffer_release(void *data, struct wl_buffer *buffer) {
	(void)(buffer); // unused!
	struct nwl_shm_buffer *nwlbuf = data;
	nwlbuf->flags = (nwlbuf->flags & ~NWL_SHM_BUFFER_ACQUIRED);
}

static const struct wl_buffer_listener buffer_listener = {
	handle_buffer_release
};

static struct nwl_shm_buffer *try_check_buffer(struct nwl_shm_bufferman *bm, struct nwl_shm_buffer *buf, int32_t offset) {
	if (buf->wl_buffer) {
		if (buf->flags & NWL_SHM_BUFFER_ACQUIRED) {
			return NULL;
		}
		if (buf->flags & NWL_SHM_BUFFER_DESTROY) {
			if (bm->impl) {
				bm->impl->buffer_destroy(buf, bm);
			}
			wl_buffer_destroy(buf->wl_buffer);
		} else {
			return buf;
		}
	}
	buf->wl_buffer = wl_shm_pool_create_buffer(bm->pool.pool, offset, bm->width, bm->height, bm->stride, bm->format);
	buf->flags = 0;
	buf->bufferdata = bm->pool.data+offset; // Ugh..
	wl_buffer_add_listener(buf->wl_buffer, &buffer_listener, buf);
	if (bm->impl) {
		bm->impl->buffer_create(buf, bm);
	}
	return buf;
}

struct nwl_shm_buffer *nwl_shm_bufferman_get_next(struct nwl_shm_bufferman *bufferman) {
	struct nwl_shm_buffer *buf = try_check_buffer(bufferman, &bufferman->buffers[0], 0);
	if (!buf) {
		buf = try_check_buffer(bufferman, &bufferman->buffers[1], bufferman->stride * bufferman->height);
	}
	return buf;
}

void nwl_shm_bufferman_resize(struct nwl_shm_bufferman *bm, struct nwl_state *state,
	uint32_t width, uint32_t height, uint32_t stride, uint32_t format) {
	size_t new_min_pool_size = (stride * height)*2;
	if (new_min_pool_size > bm->pool.size || (int)new_min_pool_size < (int)bm->pool.size-(512*1024)) {
		// Add some extra so if the surface grows slightly the pool can be reused
		nwl_shm_set_size(&bm->pool, state, new_min_pool_size+stride*30);
	}
	bm->width = width;
	bm->height = height;
	bm->stride = stride;
	bm->format = format;
	bm->buffers[0].flags |= NWL_SHM_BUFFER_DESTROY;
	bm->buffers[1].flags |= NWL_SHM_BUFFER_DESTROY;
}

void nwl_shm_destroy(struct nwl_shm_pool *shm) {
	nwl_shm_destroy_pool(shm);
	free(shm);
}

struct nwl_shm_pool *nwl_shm_create() {
	// Why be like this?
	return calloc(1, sizeof(struct nwl_shm_pool));
}

struct nwl_shm_bufferman *nwl_shm_bufferman_create() {
	// Why be like this?
	return calloc(1, sizeof(struct nwl_shm_bufferman));
}

static void destroy_buffer(struct nwl_shm_buffer *buf, struct nwl_shm_bufferman *bufferman) {
	if (buf->wl_buffer) {
		if (bufferman->impl) {
			bufferman->impl->buffer_destroy(buf, bufferman);
		}
		wl_buffer_destroy(buf->wl_buffer);
	}
}

void nwl_shm_bufferman_destroy(struct nwl_shm_bufferman *bufferman) {
	nwl_shm_destroy_pool(&bufferman->pool);
	destroy_buffer(&bufferman->buffers[0], bufferman);
	destroy_buffer(&bufferman->buffers[1], bufferman);
	free(bufferman);
}

struct nwl_shm_state_sub {
	uint32_t *formats;
	uint32_t len;
	uint32_t alloc_len;
};

static void shm_sub_destroy(void *data) {
	struct nwl_shm_state_sub *sub = data;
	free(sub->formats);
	free(sub);
}

static const struct nwl_state_sub_impl shm_subimpl = {
	shm_sub_destroy
};

static void shm_handle_format(void *data, struct wl_shm *shm, uint32_t format) {
	UNUSED(shm);
	struct nwl_shm_state_sub *sub = data;
	sub->len++;
	if (sub->len > sub->alloc_len) {
		uint32_t new_alloc_len = sub->len+8;
		sub->formats = realloc(sub->formats, sizeof(uint32_t)*new_alloc_len);
		sub->alloc_len = new_alloc_len;
	}
	sub->formats[sub->len-1] = format;
}

static const struct wl_shm_listener shm_listener = {
	shm_handle_format
};

void nwl_shm_add_listener(struct nwl_state *state) {
	void *sub = calloc(sizeof(struct nwl_shm_state_sub), 1);
	wl_shm_add_listener(state->wl.shm, &shm_listener, sub);
	nwl_state_add_sub(state, &shm_subimpl, sub);
}

void nwl_shm_get_supported_formats(struct nwl_state *state, uint32_t **formats, uint32_t *len) {
	struct nwl_shm_state_sub *sub = nwl_state_get_sub(state, &shm_subimpl);
	if (sub) {
		*len = sub->len;
		*formats = sub->formats;
	}
}
