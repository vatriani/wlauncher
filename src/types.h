#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <xkbcommon/xkbcommon.h>

#define MAX_APPS 512
#define MAX_NAME_LENGTH 256
#define MAX_MATCHED_APPS 10



struct app_info {
    char name[MAX_NAME_LENGTH];
    char exec[MAX_NAME_LENGTH];
};



struct app_context {
    struct wl_display             *display;
    struct wl_registry            *registry;
    struct wl_compositor          *compositor;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct wl_shm                 *shm;
    struct wl_seat                *seat;
    struct wl_keyboard            *keyboard;
    struct wl_surface             *surface;
    struct zwlr_layer_surface_v1  *layer_surface;
    struct wl_buffer              *buffer;

    struct xkb_context            *xkb_context;
    struct xkb_keymap             *xkb_keymap;
    struct xkb_state              *xkb_state;

    char                           input_buffer[MAX_NAME_LENGTH];
    int                            input_length;

    struct app_info                apps[MAX_APPS];
    int                            app_count;

    struct app_info                matched_apps[MAX_MATCHED_APPS];
    int                            matched_count;
    int                            matched_index;

    double                         bg_r, bg_g, bg_b;
    double                         accent_r, accent_g, accent_b;

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
};

#endif
