#include "buffer.h"

static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wlauncher-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void draw_frame(struct app_context *ctx) {
    int stride = ctx->width * 4;
    int size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    /* Hellblau-Färbung */
    for (int i = 0; i < ctx->width * ctx->height; ++i) {
        data[i] = 0xFF5294E2;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    ctx->buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
    wl_surface_damage(ctx->surface, 0, 0, ctx->width, ctx->height);
    wl_surface_commit(ctx->surface);

    munmap(data, size);
}
