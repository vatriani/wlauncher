#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE
#include "basics-t.h"
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#define MAX_APPS 512
#define MAX_NAME_LENGTH 256
#define MAX_MATCHED_APPS 10
#define MAX_APP_NAME_LENGTH 256
#define APP_NAME "wlauncher"

#define DEF_BG_COL_R         0.117    ///< fallback bg color red
#define DEF_BG_COL_G         0.117    ///< fallback bg color green
#define DEF_BG_COL_B         0.180    ///< fallback bg color blue
#define DEF_ACC_COL_R        0.321    ///< fallback accent color red
#define DEF_ACC_COL_G        0.443    ///< fallback accent color green
#define DEF_ACC_COL_B        0.654    ///< fallback accent color blue
#define DEF_FG_COL_R           0.9    ///< fallback fg color red
#define DEF_FG_COL_G           0.9    ///< fallback fg color green
#define DEF_FG_COL_B           0.9    ///< fallback fg color blue
#define DEF_PADDING              5    ///< fallback padding for drawing
#define DEF_BAR_HEIGHT          16    ///< fallback bar height
#define DEF_FONT  "DejaVu Sans 12"    ///< fallbacl font



typedef struct app_info_t app_info;
struct app_info_t {
    char name[MAX_NAME_LENGTH];
    char exec[MAX_NAME_LENGTH];
};



typedef struct color_t color;
struct color_t {
    double r;    ///< Double value for red
    double g;    ///< Double value for green
    double b;    ///< Double value for blue
};



typedef struct sorted_entry_t sorted_entry;
struct sorted_entry_t {
    const app_info *app;
    int score;
};



typedef struct render_context_t render_context;
struct render_context_t {
    struct wl_display             *display;
    struct wl_registry            *registry;
    struct wl_compositor          *compositor;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct wl_shm                 *wl_shm;
    struct wl_seat                *seat;
    struct wl_keyboard            *keyboard;
    struct wl_surface             *surface;
    struct zwlr_layer_surface_v1  *layer_surface;
    struct wl_buffer              *buffer;
    struct wl_shm_pool            *pool;
    int                            fhm_fd;
    uint32_t                      *fhm_data;
    int                            fhm_stride;
    int                            fhm_size;
    int                            buffer_busy;
    int                            needs_redraw;

    cairo_t                       *cr;
    cairo_surface_t               *cairo_surface;
    PangoLayout                   *pango_layout;
    PangoFontDescription          *pango_font_desc;
    char                          *font;

    struct xkb_context            *xkb_context;
    struct xkb_keymap             *xkb_keymap;
    struct xkb_state              *xkb_state;

    color                          bg_color;
    color                          fg_color;
    color                          accent_color;
    int                            padding;

    int                            width;
    int                            height;
};


struct app_context {
    render_context                 render;
    config_file                    config;

    char                           input_buffer[MAX_NAME_LENGTH];
    int                            input_length;
    app_info                       apps[MAX_APPS];
    int                            app_count;
    const app_info                *matched_apps[MAX_MATCHED_APPS];
    int                            matched_count;
    int                            matched_index;

    int                            running;
    int                            configured;
};

#endif
