#ifndef _NWL_SHM_H_
#define _NWL_SHM_H_
#include <unistd.h>
#include <stdint.h>

struct nwl_state;

struct nwl_surface_shm {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
	char *name;
	struct wl_buffer *buffer;
};

int nwl_allocate_shm_file(size_t size);
void nwl_shm_set_size(struct nwl_surface_shm *shm, struct nwl_state *state, size_t pool_size);
void nwl_shm_destroy_pool(struct nwl_surface_shm *shm);
void nwl_shm_destroy(struct nwl_surface_shm *shm);
// format is enum wl_shm_format
struct wl_buffer *nwl_shm_get_buffer(struct nwl_surface_shm *shm, uint32_t width,
		uint32_t height, uint32_t stride, uint32_t format);
struct nwl_surface_shm *nwl_shm_create();
#endif
