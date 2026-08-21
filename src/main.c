#define _GNU_SOURCE

#include "types.h"
#include "basics.h"
#include "wayland-core.h"
#include "parser.h"
#include "buffer.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <wayland-client.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

// forward declarations
static int setup_ctx(struct app_context *ctx);
static void cleanup(struct app_context *ctx);
static int config_fill_defaults(struct app_context *ctx);
static void setConfigValues(struct app_context *ctx);



// static pointer, only visible for aexit
static struct app_context *atexit_ctx_ptr = NULL;
// wrapper-function without param for atexit
void atexit_wrapper(void) {
    if (atexit_ctx_ptr) {
        cleanup(atexit_ctx_ptr);
    }
}



int main(int argc, char **argv) {
    struct app_context stack_ctx;
    register struct app_context *ctx = &stack_ctx;
    memset(ctx, 0, sizeof(struct app_context));


    //ctx->width = 0;
    //ctx->height = 24;

#ifndef DEBUG
    if (checkIfRunning()) return 0;
#endif
    zombieProtect();
    if (optHandling(argc, argv, ctx)) return -1;
    if (configLoad(&ctx->config)) {
        printf("[wlauncher] falling back to default values\n");
        config_fill_defaults(ctx);
    }
    else {
        setConfigValues(ctx);
        configFree(&ctx->config);
    }

    scan_applications(ctx);
    find_best_matches(ctx, "");
    setup_ctx(ctx);
    setupCairo(ctx);

#ifdef DEBUG
    printf("wlauncher: Waiting for input...\n");
#endif
    while (ctx->running && wl_display_dispatch(ctx->render.display) != -1) {}
#ifdef DEBUG
    printf("wlauncher: close program and cleanup memory.\n");
#endif


    cleanup(ctx);

    return 0;
}


static int setup_ctx(struct app_context *ctx) {
    ctx->render.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    ctx->render.display = wl_display_connect(NULL);
    if (!ctx->render.display) return 1;

    ctx->render.registry = wl_display_get_registry(ctx->render.display);
    wl_registry_add_listener(ctx->render.registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->render.display);

    if (!ctx->render.compositor || !ctx->render.layer_shell || !ctx->render.shm) {
#ifdef DEBGUG
        fprintf(stderr, "Err: crittical Wayland handler missing.\n");
#endif
        wl_display_disconnect(ctx->render.display);
        return -1;
    }

    ctx->render.surface = wl_compositor_create_surface(ctx->render.compositor);
    ctx->render.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            ctx->render.layer_shell, ctx->render.surface, NULL,
            ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wlauncher");

    uint32_t anchors = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(ctx->render.layer_surface, anchors);
    zwlr_layer_surface_v1_set_size(ctx->render.layer_surface, 0,
            ctx->render.height);
    zwlr_layer_surface_v1_set_exclusive_zone(ctx->render.layer_surface,
            ctx->render.height);
    zwlr_layer_surface_v1_set_keyboard_interactivity(ctx->render.layer_surface,
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    zwlr_layer_surface_v1_add_listener(ctx->render.layer_surface,
            &layer_surface_listener, ctx);

    wl_surface_commit(ctx->render.surface);
    wl_display_flush(ctx->render.display);

    ctx->running = 1;
    return 0;
}

static void cleanup(struct app_context *ctx) {
    if (ctx->render.surface) {
        wl_surface_attach(ctx->render.surface, NULL, 0, 0);
        wl_surface_commit(ctx->render.surface);
    }
    wl_display_flush(ctx->render.display);

    if (ctx->render.display)       wl_display_roundtrip(ctx->render.display);
    if (ctx->render.keyboard)      wl_keyboard_destroy(ctx->render.keyboard);
    if (ctx->render.seat)          wl_seat_destroy(ctx->render.seat);
    if (ctx->render.layer_surface) zwlr_layer_surface_v1_destroy(ctx->render.layer_surface);
    if (ctx->render.surface)       wl_surface_destroy(ctx->render.surface);
    if (ctx->render.buffer)        wl_buffer_destroy(ctx->render.buffer);
    if (ctx->render.xkb_state)     xkb_state_unref(ctx->render.xkb_state);
    if (ctx->render.xkb_keymap)    xkb_keymap_unref(ctx->render.xkb_keymap);
    if (ctx->render.xkb_context)   xkb_context_unref(ctx->render.xkb_context);
    if (ctx->render.display)       wl_display_disconnect(ctx->render.display);
}



static void setConfigValues(struct app_context *ctx) {
    ctx->render.padding = atoi(configGetValueFromName(&ctx->config, "padding"));
    strncpy(ctx->render.font, configGetValueFromName(&ctx->config, "font"),MAX_APP_NAME_LENGTH);
    ctx->render.bg_color =  rgb_to_double(configGetValueFromName(&ctx->config, "bg_color"));
    ctx->render.fg_color =  rgb_to_double(configGetValueFromName(&ctx->config, "fg_color"));
    ctx->render.accent_color =  rgb_to_double(configGetValueFromName(&ctx->config, "ac_color"));
    ctx->render.height = atoi(configGetValueFromName(&ctx->config, "bar_height"));
}



static int config_fill_defaults(struct app_context *ctx) {
    ctx->render.bg_color.r = DEF_BG_COL_R;
    ctx->render.bg_color.g = DEF_BG_COL_G;
    ctx->render.bg_color.b = DEF_BG_COL_B;
    ctx->render.accent_color.r = DEF_ACC_COL_R;
    ctx->render.accent_color.g = DEF_ACC_COL_G;
    ctx->render.accent_color.b = DEF_ACC_COL_B;
    ctx->render.fg_color.r = DEF_FG_COL_R;
    ctx->render.fg_color.g = DEF_FG_COL_G;
    ctx->render.fg_color.b = DEF_FG_COL_B;
    ctx->render.padding = DEF_PADDING;
    ctx->render.height = DEF_BAR_HEIGHT;

    ctx->render.font = calloc(MAX_APP_NAME_LENGTH, sizeof(char));
    if (!ctx->render.font) return -1;
    if (ctx->render.font) strcpy(ctx->render.font, DEF_FONT);

    return 0;
}
