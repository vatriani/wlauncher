/**
 *  \file       types.h
 *  \brief      Defines the datastructure for the wayland configuration
 *  \author     Niels Neumann
 *  \version    0.1
 *  \date       2026
 *  \copyright  GNU Public License v3
 */

#ifdef TYPES_H
#define TYPES_H

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wayland-client.h>

/* Die vom Scanner generierten Header */
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

struct app_context {
    struct wl_display             *display;
    struct wl_registry            *registry;
    struct wl_compositor          *compositor;
    struct zwlr_layer_shell_v1    *layer_shell;
    struct wl_shm                 *shm;

    struct wl_surface             *surface;
    struct zwlr_layer_surface_v1  *layer_surface;
    struct wl_buffer              *buffer;

    int                            running;
    int                            width;
    int                            height;
    int                            configured;
};

#endif
