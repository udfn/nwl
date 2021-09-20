#ifndef _NWL_SHM_H_
#define _NWL_SHM_H_
#include <unistd.h>
#include <stdint.h>

struct nwl_surface;

struct nwl_surface_shm {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
	int32_t stride;
	char *name;
	struct wl_buffer *buffer;
};

int nwl_allocate_shm_file(size_t size);
void nwl_surface_render_backend_shm(struct nwl_surface *surface);

#endif
