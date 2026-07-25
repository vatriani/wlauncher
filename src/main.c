#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h> // NEU: Für Zombie-Prozess Handling

#include "types.h"
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "wayland-core.h"
#include "window.h"
#include "parser.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* 1. ZOMBIE-SCHUTZ: Ignoriert das CHLD-Signal.
       Der Kernel raeumt geforkte Prozesse (Steam, Kate) sofort atomar auf (0% PID-Pollution) */
    signal(SIGCHLD, SIG_IGN);

    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));
    ctx->running = 1;
    ctx->width = 0;
    ctx->height = 24;

    scan_applications(ctx);
    fetch_hyprland_colors(ctx);

    ctx->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    ctx->display = wl_display_connect(NULL);
    if (!ctx->display) return 1;

    ctx->registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->display);

    if (!ctx->compositor || !ctx->layer_shell || !ctx->shm) {
        fprintf(stderr, "Fehler: Kritische Wayland-Schnittstellen fehlen.\n");
        wl_display_disconnect(ctx->display);
        return 1;
    }

    ctx->surface = wl_compositor_create_surface(ctx->compositor);
    ctx->layer_surface = zwlr_layer_shell_v1_get_layer_surface(ctx->layer_shell, ctx->surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    /* LAYER SHELL ANCHOR: Saubere, explizite Bitmasken-Zuweisung */
    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->layer_surface, 0, ctx->height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->layer_surface, ctx->height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(ctx->layer_surface, &layer_surface_listener, ctx);

    wl_surface_commit(ctx->surface);
    wl_display_flush(ctx->display);

    printf("wlauncher: System stabil und zombie-geschuetzt. Warte auf Eingabe...\n");

    while (ctx->running && wl_display_dispatch(ctx->display) != -1) {}

    printf("wlauncher: Beende Programm und raeume Speicher auf.\n");

    /* 2. REGISTRY CLEANUP FIX: Erst die gebundenen Interfaces loeschen, dann die Core-Schnittstellen */
    if (ctx->keyboard)      wl_keyboard_destroy(ctx->keyboard);
    if (ctx->seat)          wl_seat_destroy(ctx->seat);
    if (ctx->buffer)        wl_buffer_destroy(ctx->buffer);
    if (ctx->layer_surface) zwlr_layer_surface_v1_destroy(ctx->layer_surface);
    if (ctx->surface)       wl_surface_destroy(ctx->surface);

    /* Globale Destruktoren aufrufen (Schließt alle offenen Server-Schnittstellen leckfrei) */
    if (ctx->layer_shell)   zwlr_layer_shell_v1_destroy(ctx->layer_shell);
    if (ctx->compositor)    wl_compositor_destroy(ctx->compositor);
    if (ctx->shm)           wl_shm_destroy(ctx->shm);
    if (ctx->registry)      wl_registry_destroy(ctx->registry);

    if (ctx->xkb_state)   xkb_state_unref(ctx->xkb_state);
    if (ctx->xkb_keymap)  xkb_keymap_unref(ctx->xkb_keymap);
    if (ctx->xkb_context) xkb_context_unref(ctx->xkb_context);

    wl_display_flush(ctx->display);
    wl_display_disconnect(ctx->display);
    return 0;
}
