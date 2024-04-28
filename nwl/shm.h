#ifndef _NWL_SHM_H_
#define _NWL_SHM_H_
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

struct wl_shm;
struct nwl_state;

struct nwl_shm_pool {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
};

enum nwl_shm_buffer_flags {
	NWL_SHM_BUFFER_ACQUIRED = 1 << 0,
	NWL_SHM_BUFFER_DESTROY = 1 << 1
};

struct nwl_shm_buffer {
	struct wl_buffer *wl_buffer;
	uint8_t *bufferdata;
	char flags; // nwl_shm_buffer_flags
};

#define NWL_SHM_BUFFERMAN_MAX_BUFFERS 4

struct nwl_shm_bufferman {
	struct nwl_shm_pool pool;
	struct nwl_shm_buffer buffers[NWL_SHM_BUFFERMAN_MAX_BUFFERS];
	struct nwl_shm_bufferman_renderer_impl *impl;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	uint8_t num_slots;
};

struct nwl_shm_bufferman_renderer_impl {
	void (*buffer_create)(unsigned int buffer_idx, struct nwl_shm_bufferman *bufferman);
	void (*buffer_destroy)(unsigned int buffer_idx, struct nwl_shm_bufferman *bufferman);
};

int nwl_allocate_shm_file(size_t size);
void nwl_shm_set_size(struct nwl_shm_pool *shm, struct wl_shm *wl_shm, size_t pool_size);
void nwl_shm_pool_finish(struct nwl_shm_pool *shm);
void nwl_shm_destroy(struct nwl_shm_pool *shm);

// returns the buffer index, or -1 if there is no available buffer
int nwl_shm_bufferman_get_next(struct nwl_shm_bufferman *bufferman);

// format is enum wl_shm_format
void nwl_shm_bufferman_resize(struct nwl_shm_bufferman *bufferman, struct wl_shm *wl_shm,
	uint32_t width, uint32_t height, uint32_t stride, uint32_t format);
void nwl_shm_bufferman_init(struct nwl_shm_bufferman *bufferman);
void nwl_shm_bufferman_finish(struct nwl_shm_bufferman *bufferman);
void nwl_shm_get_supported_formats(struct nwl_state *state, uint32_t **formats, uint32_t *len);
// Set max amount of buffers
void nwl_shm_bufferman_set_slots(struct nwl_shm_bufferman *bufferman, struct wl_shm *wl_shm, uint8_t num_slots);
#endif
