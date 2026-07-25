#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "../include/types.h"

struct app_context;

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version);
static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id);
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height);
static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface);

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wlauncher-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    register struct app_context *ctx = (struct app_context *)data;
    if (strncmp(interface, "wl_compositor", 13) == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 4);
    } else if (strncmp(interface, "wl_shm", 6) == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

static void draw_frame(struct app_context *ctx) {
    int stride = ctx->width * 4;
    int size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    /* Knalliges Hellblau (#5294e2) passend zu deiner Conky-Farbe, damit du den Balken unmissverständlich siehst! */
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

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
    register struct app_context *ctx = (struct app_context *)data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    /* Nutzt die exakten Monitor-Maße, die Hyprland uns mitteilt */
    if (width > 0) ctx->width = width;
    if (height > 0) ctx->height = height;

    /* Zeichnet den Frame erst, wenn Hyprland uns die Konfiguration bestätigt */
    if (!ctx->configured) {
        ctx->configured = 1;
        draw_frame(ctx);
    }
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
    register struct app_context *ctx = (struct app_context *)data;
    ctx->running = 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));
    ctx->running = 1;
    ctx->width = 2560; // Initialer Schätzwert für deinen 1440p Monitor
    ctx->height = 24;  // Feste Höhe der dmenu-Leiste

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Fehler: Benötigte Wayland-Schnittstellen fehlen.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);

    /* NEU: Sagt Hyprland, dass andere Fenster um 24 Pixel nach unten verschoben werden müssen */
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);

    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    /* Initialer Commit, um das configure-Event auf dem Server auszulösen */
    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);

    printf("wlauncher: Verbindung steht. Starte Event-Schleife (Drücke eine Taste zum Beenden)...\n");

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    /* Ungepufferte Schleife: Wartet exzessiv auf Eingaben */
    while (ctx->running && wl_display_dispatch(ctx->display) != -1) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_sec - start_time.tv_sec >= 3) {
            ctx->running = 0;
        }
    }

    if (ctx->buffer)        wl_buffer_destroy(ctx->buffer);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->surface)       wl_surface_destroy(ctx->surface);
    if (ctx->layer_shell)   zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    if (ctx->compositor)    wl_compositor_destroy(ctx->compositor);
    if (ctx->shm)           wl_shm_destroy(ctx->shm);
    if (ctx->registry)      wl_registry_destroy(ctx->registry);

    wl_display_flush(ctx->display);
    wl_display_disconnect(ctx->display);
    return 0;
}
