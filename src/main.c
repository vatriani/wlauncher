#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <wayland-client.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "wayland-core.h"
#include "window.h"
#include "parser.h"



int main(int argc, char **argv) {
    (void)argc; (void)argv;

    int instance_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (instance_sock >= 0) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        strncpy(addr.sun_path + 1, "wlauncher_single_instance_lock", sizeof(addr.sun_path) - 2);

        if (bind(instance_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            if (errno == EADDRINUSE) {
#ifdef DEBUG
                fprintf(stderr, "wlauncher: is already running\n");
#endif
                close(instance_sock);
                return 0;
            }
        }
    }

    signal(SIGCHLD, SIG_IGN);

    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));
    ctx->running = 1;
    ctx->width = 0;
    ctx->height = 24;

    scan_applications(ctx);
    find_best_matches(ctx, "");
    fetch_hyprland_colors(ctx);

    ctx->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
#ifdef DEBGUG
        fprintf(stderr, "Err: crittical Wayland handler missing.\n");
#endif
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);

#ifdef DEBUG
    printf("wlauncher: Waiting for input...\n");
#endif
    while (ctx->running && wl_display_dispatch(ctx->display) != -1) {}
#ifdef DEBUG
    printf("wlauncher: close program and cleanup memory.\n");
#endif


    if (ctx->surface) {
        wl_surface_attach(ctx->surface, NULL, 0, 0);
        wl_surface_commit(ctx->surface);
    }
    wl_display_flush(ctx->display);

    if (ctx->display)       wl_display_roundtrip(ctx->display);
    if (ctx->keyboard)      wl_keyboard_destroy(ctx->keyboard);
    if (ctx->seat)          wl_seat_destroy(ctx->seat);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->surface)       wl_surface_destroy(ctx->surface);
    if (ctx->buffer)        wl_buffer_destroy(ctx->buffer);
    if (ctx->xkb_state)     xkb_state_unref(ctx->xkb_state);
    if (ctx->xkb_keymap)    xkb_keymap_unref(ctx->xkb_keymap);
    if (ctx->xkb_context)   xkb_context_unref(ctx->xkb_context);
    if (ctx->display)       wl_display_disconnect(ctx->display);

    return 0;
}
