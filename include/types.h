/**
 *  \file       types.h
 *  \brief      Defines the datastructure for the wayland configuration
 *  \author     Niels Neumann
 *  \version    0.1
 *  \date       2026
 *  \copyright  GNU Public License v3
 */

#ifdef __TYPES_H__
#define __TYPES_H__

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
