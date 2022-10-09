#ifndef _NWL_SHM_H_
#define _NWL_SHM_H_
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

struct nwl_state;

struct nwl_shm_pool {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
	char *name;
};

enum nwl_shm_buffer_flags {
	NWL_SHM_BUFFER_ACQUIRED = 1 << 0,
	NWL_SHM_BUFFER_DESTROY = 1 << 1
};

struct nwl_shm_buffer {
	struct wl_buffer *wl_buffer;
	uint8_t *bufferdata;
	void *data;
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
	void (*buffer_create)(struct nwl_shm_buffer *buffer, struct nwl_shm_bufferman *bufferman);
	void (*buffer_destroy)(struct nwl_shm_buffer *buffer, struct nwl_shm_bufferman *bufferman);
};

int nwl_allocate_shm_file(size_t size);
void nwl_shm_set_size(struct nwl_shm_pool *shm, struct nwl_state *state, size_t pool_size);
void nwl_shm_destroy_pool(struct nwl_shm_pool *shm);
void nwl_shm_destroy(struct nwl_shm_pool *shm);
struct nwl_shm_pool *nwl_shm_create(void);

struct nwl_shm_buffer *nwl_shm_bufferman_get_next(struct nwl_shm_bufferman *bufferman);
// format is enum wl_shm_format
void nwl_shm_bufferman_resize(struct nwl_shm_bufferman *bufferman, struct nwl_state *state,
	uint32_t width, uint32_t height, uint32_t stride, uint32_t format);
struct nwl_shm_bufferman *nwl_shm_bufferman_create(void);
void nwl_shm_bufferman_destroy(struct nwl_shm_bufferman *bufferman);
void nwl_shm_get_supported_formats(struct nwl_state *state, uint32_t **formats, uint32_t *len);
// Set max amount of buffers
void nwl_shm_bufferman_set_slots(struct nwl_shm_bufferman *bufferman, struct nwl_state *state, uint8_t num_slots);
// If a buffer was taken with nwl_shm_bufferman_get_next and was then not sent to the compositor.
void nwl_shm_buffer_release(struct nwl_shm_buffer* buffer);
#endif
