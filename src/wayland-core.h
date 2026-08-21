#ifndef WAYLAND_CORE_H
#define WAYLAND_CORE_H

#include "types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include <wayland-client.h>

extern const struct wl_registry_listener registry_listener;
extern const struct zwlr_layer_surface_v1_listener layer_surface_listener;

#endif
