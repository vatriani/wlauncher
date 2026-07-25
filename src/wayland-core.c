#include "wayland-core.h"

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    register struct app_context *ctx = (struct app_context *)data;
    if (strncmp(interface, "wl_compositor", 13) == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 4);
    } else if (strncmp(interface, "wl_shm", 6) == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
