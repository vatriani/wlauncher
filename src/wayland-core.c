#include "types.h"
#include "wayland-core.h"
#include "buffer.h"
#include "parser.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>



static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int32_t fd, uint32_t size) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (ctx->render.xkb_keymap) xkb_keymap_unref(ctx->render.xkb_keymap);
    if (ctx->render.xkb_state)  xkb_state_unref(ctx->render.xkb_state);

    ctx->render.xkb_keymap = xkb_keymap_new_from_string(ctx->render.xkb_context,
            map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (!ctx->render.xkb_keymap) return;
    ctx->render.xkb_state = xkb_state_new(ctx->render.xkb_keymap);
}



static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
#ifdef DEBUG
    printf("wlauncher: Tastatur-Fokus ERHALTEN.\n");
#endif
}



static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
#ifdef DEBUG
    printf("wlauncher: Tastatur-Fokus VERLOREN.\n");
#endif
}



/* MODIFIERS-EVENT */
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)keyboard; (void)serial;
    if (ctx->render.xkb_state) {
        xkb_state_update_mask(ctx->render.xkb_state, mods_depressed,
                mods_latched, mods_locked, 0, 0, group);
    }
}



static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}



/* KEY-EVENT */
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)serial; (void)time; (void)keyboard;

    if (state == 1) {
        uint32_t xkb_keycode = key + 8;

        if (key == KEY_ESC) {
            ctx->running = 0;
            return;
        }

        if (key == KEY_RIGHT && ctx->matched_count > 0) {
            ctx->matched_index = (ctx->matched_index + 1) % ctx->matched_count;
            if (ctx->configured && ctx->render.width > 0) draw_frame(ctx);
            return;
        }

        if (key == KEY_LEFT && ctx->matched_count > 0) {
            ctx->matched_index = (ctx->matched_index - 1 + ctx->matched_count)
                    % ctx->matched_count;
            if (ctx->configured && ctx->render.width > 0) draw_frame(ctx);
            return;
        }

        if (key == KEY_ENTER) {
           const char *exec_cmd = NULL;

            if (ctx->matched_count > 0 && ctx->matched_index <
                    ctx->matched_count) {
               exec_cmd = ctx->matched_apps[ctx->matched_index]->exec;
#ifdef DEBUG
               printf("wlauncher: Starte Vorschlag [%d]: %s via '%s'\n",
                      ctx->matched_index, ctx->matched_apps[ctx->matched_index]->name, exec_cmd);
#endif
            } else
               exec_cmd = ctx->input_buffer;

            if (exec_cmd && exec_cmd[0] != '\0') {
                pid_t pid = fork();
                if (pid == 0) {
                    setsid();

                    for (int i = 3; i < 1024; ++i) close(i);

                    int devnull = open("/dev/null", O_WRONLY);
                    if (devnull >= 0) {
                        dup2(devnull, STDOUT_FILENO);
                        dup2(devnull, STDERR_FILENO);
                        close(devnull);
                    }

                    execl("/bin/sh", "sh", "-c", exec_cmd, (char *)NULL);
                    _exit(1);
                } else if (pid < 0) {
                    perror("wlauncher: fork error");
                }
            }
            ctx->running = 0;
            return;
        }

        if (key == KEY_BACKSPACE && ctx->input_length > 0) {
            ctx->input_buffer[--ctx->input_length] = '\0';
            ctx->matched_index = 0;

            find_best_matches(ctx, ctx->input_buffer);

#ifdef DEBUG
            printf("input: %s (matches: %d)\n", ctx->input_buffer,
                    ctx->matched_count);
#endif
            if (ctx->configured && ctx->render.width > 0) draw_frame(ctx);
            return;
        }

        char utf8_buf[8] = {0};
        if (ctx->render.xkb_state && xkb_state_key_get_utf8(
                    ctx->render.xkb_state, xkb_keycode, utf8_buf,
                    sizeof(utf8_buf)) > 0) {
            char c = utf8_buf[0];

            if (c >= 32 && ctx->input_length < 255) {
                ctx->input_buffer[ctx->input_length++] = c;
                ctx->input_buffer[ctx->input_length] = '\0';
                ctx->matched_index = 0;

                find_best_matches(ctx, ctx->input_buffer);

#ifdef DEBUG
                printf("input: %s (matches: %d)\n", ctx->input_buffer,
                        ctx->matched_count);
#endif
                if (ctx->configured && ctx->render.width > 0) draw_frame(ctx);
            }
        }
    }
}



static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_handle_keymap,
    .enter = keyboard_handle_enter,
    .leave = keyboard_handle_leave,
    .key = keyboard_handle_key,
    .modifiers = keyboard_handle_modifiers,
    .repeat_info = keyboard_handle_repeat_info,
};



static void seat_handle_capabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities) {
    register struct app_context *ctx = (struct app_context *)data;
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        ctx->render.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->render.keyboard, &keyboard_listener, ctx);
    }
}



static void seat_handle_name(void *data, struct wl_seat *seat,
        const char *name) {
    (void)data; (void)seat; (void)name;
}



static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    register struct app_context *ctx = (struct app_context *)data;

    if (!ctx) return;

    if (strncmp(interface, "wl_compositor", 13) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->render.compositor = wl_registry_bind(registry, id,
                &wl_compositor_interface, bind_ver);
    }
    else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->render.layer_shell = wl_registry_bind(registry, id,
                &zwlr_layer_shell_v1_interface, bind_ver);
    }
    else if (strncmp(interface, "wl_shm", 6) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->render.wl_shm = wl_registry_bind(registry, id, &wl_shm_interface,
                bind_ver);
    }
    else if (strncmp(interface, "wl_seat", 7) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->render.seat = wl_registry_bind(registry, id, &wl_seat_interface,
                bind_ver);
        wl_seat_add_listener(ctx->render.seat, &seat_listener, ctx);
    }
}



void registry_handle_global_remove(void *data, struct wl_registry *registry,
        uint32_t id) {
    (void)data; (void)registry; (void)id;
}



const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

static void output_handle_geometry(void *data, struct wl_output *wl_output,
        int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
        int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y; (void)physical_width;
    (void)physical_height; (void)subpixel; (void)make; (void)model;
    (void)transform;
}



static void output_handle_mode(void *data, struct wl_output *wl_output,
        uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    (void)data; (void)wl_output; (void) height; (void)refresh;

    struct app_context *ctx = data;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        if (ctx->render.width <= 0) {
            ctx->render.width = width;
        }
    }
}



static void output_handle_done(void *data, struct wl_output *wl_output) {
    (void)data; (void)wl_output;
}



static void output_handle_scale(void *data, struct wl_output *wl_output,
        int32_t factor) {
    (void)data; (void)wl_output; (void)factor;
}



static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode = output_handle_mode,
    .done = output_handle_done,
    .scale = output_handle_scale,
};



static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial,
        uint32_t width, uint32_t height) {
    register struct app_context *ctx = (struct app_context *)data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (width > 0) ctx->render.width = width;
    if (height > 0) ctx->render.height = height;

#ifdef DEBUG
    fprintf(stderr, "configure: %ux%u ready=%d\n", width, height, ctx->configured);
#endif
    if (ctx->render.width > 0 && ctx->configured) {
        ctx->render.buffer_busy = 0;
        if (ctx->render.needs_redraw) {
            ctx->render.needs_redraw = 0;
            draw_frame(ctx);
        }
    }
}




static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)layer_surface;
    ctx->running = 0;
}



const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};
