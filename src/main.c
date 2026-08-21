#define _GNU_SOURCE

#include "types.h"
#include "basics.h"
#include "wayland-core.h"
#include "parser.h"
#include "buffer.h"
#include "cache.h"

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

    vector_init(&ctx->apps);

    atexit_ctx_ptr = ctx;
    atexit(atexit_wrapper);

#ifndef DEBUG
    if (checkIfRunning()) return 0;
#endif
    ctx->force_rebuild_cache = 0;
    zombieProtect();
    config_fill_defaults(ctx);
    if (optHandling(argc, argv, ctx)) return -1;
    if (configLoad(&ctx->config))
        printf("[wlauncher] falling back to default values\n");
    else {
        setConfigValues(ctx);
        configFree(&ctx->config);
    }

    if (!ctx->force_rebuild_cache && cache_load_if_valid(ctx) == 1) {
    } else {
        scan_applications(ctx);
        cache_store(ctx);
    }
    find_best_matches(ctx, "");

    if (setup_ctx(ctx) != 0) return -1;

    /* Warten bis initiales configure verarbeitet wurde */
    wl_display_roundtrip(ctx->render.display);

    if (ctx->render.width <= 0 || ctx->render.height <= 0) {
        fprintf(stderr, "No valid configure size received\n");
        return -1;
    }

    if (setupCairo(ctx) != 0) {
        fprintf(stderr, "setupCairo failed\n");
        return -1;
    }

    if (ctx->render.width > 0 && ctx->render.height > 0) {
        ctx->render.buffer_busy = 0;
        draw_frame(ctx);
    }

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
    ctx->render.width = 0;

    ctx->render.display = wl_display_connect(NULL);
    if (!ctx->render.display) return 1;

    ctx->render.registry = wl_display_get_registry(ctx->render.display);
    wl_registry_add_listener(ctx->render.registry, &registry_listener, ctx);
    wl_display_roundtrip(ctx->render.display);

    if (!ctx->render.compositor || !ctx->render.layer_shell || !ctx->render.wl_shm) {
#ifdef DEBUG
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



static void free_apps_vector(struct app_context *ctx) {
    if (!ctx) return;
    if (!ctx->apps.pfVectorTotal || !ctx->apps.pfVectorGet || !ctx->apps.pfVectorFree) return;

    int total = ctx->apps.pfVectorTotal(&ctx->apps);
    for (int i = 0; i < total; ++i) {
        app_info *app = (app_info *)ctx->apps.pfVectorGet(&ctx->apps, i);
        free(app);
    }
    ctx->apps.pfVectorFree(&ctx->apps);
}



static void cleanup(struct app_context *ctx) {
    if (!ctx) return;

    /* 1) Cairo/SHM zuerst */
    cairo_cleanup(ctx);

    /* 2) Surface sauber ablösen */
    if (ctx->render.surface) {
        wl_surface_attach(ctx->render.surface, NULL, 0, 0);
        wl_surface_commit(ctx->render.surface);
    }

    if (ctx->render.display) {
        wl_display_flush(ctx->render.display);
        wl_display_roundtrip(ctx->render.display);
    }

    /* 3) Wayland input/shell objects */
    if (ctx->render.keyboard) {
        wl_keyboard_destroy(ctx->render.keyboard);
        ctx->render.keyboard = NULL;
    }

    if (ctx->render.seat) {
        wl_seat_destroy(ctx->render.seat);
        ctx->render.seat = NULL;
    }

    if (ctx->render.layer_surface) {
        zwlr_layer_surface_v1_destroy(ctx->render.layer_surface);
        ctx->render.layer_surface = NULL;
    }

    if (ctx->render.surface) {
        wl_surface_destroy(ctx->render.surface);
        ctx->render.surface = NULL;
    }

    if (ctx->render.registry) {
        wl_registry_destroy(ctx->render.registry);
        ctx->render.registry = NULL;
    }

    if (ctx->render.compositor) {
        wl_compositor_destroy(ctx->render.compositor);
        ctx->render.compositor = NULL;
    }

    if (ctx->render.wl_shm) {
        wl_shm_destroy(ctx->render.wl_shm);
        ctx->render.wl_shm = NULL;
    }

    if (ctx->render.layer_shell) {
        zwlr_layer_shell_v1_destroy(ctx->render.layer_shell);
        ctx->render.layer_shell = NULL;
    }

    /* 4) XKB */
    if (ctx->render.xkb_state) {
        xkb_state_unref(ctx->render.xkb_state);
        ctx->render.xkb_state = NULL;
    }

    if (ctx->render.xkb_keymap) {
        xkb_keymap_unref(ctx->render.xkb_keymap);
        ctx->render.xkb_keymap = NULL;
    }

    if (ctx->render.xkb_context) {
        xkb_context_unref(ctx->render.xkb_context);
        ctx->render.xkb_context = NULL;
    }

    /* 5) display zuletzt */
    if (ctx->render.display) {
        wl_display_disconnect(ctx->render.display);
        ctx->render.display = NULL;
    }

    if (ctx->render.font) {
        free(ctx->render.font);
        ctx->render.font = NULL;
    }

    free_apps_vector(ctx);
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
