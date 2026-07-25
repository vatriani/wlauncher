#include "types.h"
#include "wayland-core.h"
#include "window.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));
    ctx->running = 1;
    ctx->width = 2560;
    ctx->height = 24;

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Fehler: Schnittstellen fehlen.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);

    printf("wlauncher: Modularisiertes System geladen. Starte Schleife...\n");

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

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
