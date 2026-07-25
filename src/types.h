#ifndef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <xkbcommon/xkbcommon.h>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct zwlr_layer_shell_v1;
struct wl_shm;
struct wl_surface;
struct zwlr_layer_surface_v1;
struct wl_buffer;
struct wl_seat;
struct wl_keyboard;

#define MAX_APPS 512

struct app_info {
    char name[256];       /* Der Anzeigename (z.B. "Konsole") */
    char exec[256];      /* Der echte Befehl (z.B. "konsole") */
};

struct app_context {
    struct wl_display             *display;
    struct wl_registry            *registry;
    struct wl_compositor          *compositor;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct wl_shm                 *shm;

    struct wl_seat                *seat;
    struct wl_keyboard            *keyboard;
    struct xkb_context            *xkb_context;
    struct xkb_keymap             *xkb_keymap;
    struct xkb_state              *xkb_state;
    char                           input_buffer[256];
    int                            input_length;

    struct wl_surface             *surface;
    struct zwlr_layer_surface_v1  *layer_surface;
    struct wl_buffer              *buffer;

    struct app_info                apps[MAX_APPS];
    int                            app_count;

    struct app_info matched_apps[5]; /* Speicher für die aktuellen Top 5 Treffer */
    int             matched_count;   /* Wie viele Treffer wurden real gefunden (0-5) */
    int             matched_index;   /* Welcher Treffer ist gerade ausgewählt (0-4) */

    double bg_r, bg_g, bg_b;     /* Hintergrundfarbe */
    double accent_r, accent_g, accent_b; /* Deine aktive Rahmenfarbe für den Fokus */

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
};

#endif
