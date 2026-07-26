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



static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
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

    if (ctx->xkb_keymap) xkb_keymap_unref(ctx->xkb_keymap);
    if (ctx->xkb_state)  xkb_state_unref(ctx->xkb_state);

    ctx->xkb_keymap = xkb_keymap_new_from_string(ctx->xkb_context, map_str, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map_str, size);
    close(fd);

    if (!ctx->xkb_keymap) return;
    ctx->xkb_state = xkb_state_new(ctx->xkb_keymap);
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
    if (ctx->xkb_state) {
        xkb_state_update_mask(ctx->xkb_state, mods_depressed, mods_latched, mods_locked, 0, 0, group);
    }
}



static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}



/* KEY-EVENT */
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
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
            if (ctx->configured && ctx->width > 0) draw_frame(ctx);
            return;
        }

        if (key == KEY_LEFT && ctx->matched_count > 0) {
            ctx->matched_index = (ctx->matched_index - 1 + ctx->matched_count) % ctx->matched_count;
            if (ctx->configured && ctx->width > 0) draw_frame(ctx);
            return;
        }

        if (key == KEY_ENTER) {
            char *exec_cmd = NULL;

            if (ctx->matched_count > 0 && ctx->matched_index < ctx->matched_count) {
                exec_cmd = ctx->matched_apps[ctx->matched_index].exec;
#ifdef DEBUG
                printf("wlauncher: Starte Vorschlag [%d]: %s via '%s'\n",
                       ctx->matched_index, ctx->matched_apps[ctx->matched_index].name, exec_cmd);
#endif
            } else {
                exec_cmd = ctx->input_buffer;
            }

            if (exec_cmd && exec_cmd[0] != '\0') {
                pid_t pid = fork();
                if (pid == 0) {
                    setsid();

                    int devnull = open("/dev/null", O_WRONLY);
                    dup2(devnull, STDOUT_FILENO);
                    dup2(devnull, STDERR_FILENO);
                    close(devnull);

                    char *args[64];
                    int arg_count = 0;
                    char cmd_copy[256];
                    strncpy(cmd_copy, exec_cmd, sizeof(cmd_copy) - 1);
                    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

                    register char *token = cmd_copy;
                    while (*token && arg_count < 63) {
                        while (*token == ' ') ++token;
                        if (*token == '\0') break;
                        args[++arg_count] = token;
                        while (*token && *token != ' ') ++token;

                        if (*token == ' ') {
                            *token = '\0';
                            ++token;
                        }
                    }
                    args[arg_count] = NULL;

                    if (arg_count > 0) execvp(args[0], args);
                   _exit(1);
                }
            }
            ctx->running = 0;
            return;
        }

        if (key == KEY_BACKSPACE && ctx->input_length > 0) {
            ctx->input_buffer[--ctx->input_length] = '\0';
            ctx->matched_index = 0;

            if (ctx->configured && ctx->width > 0) draw_frame(ctx);
#ifdef DEBUG
            printf("Eingabe: %s\n", ctx->input_buffer);
#endif
            return;
        }

        char utf8_buf[8] = {0};
        if (ctx->xkb_state && xkb_state_key_get_utf8(ctx->xkb_state, xkb_keycode, utf8_buf, sizeof(utf8_buf)) > 0) {
            char c = utf8_buf[0];

            if (c >= 32 && ctx->input_length < 255) {
                ctx->input_buffer[ctx->input_length++] = c;
                ctx->input_buffer[ctx->input_length] = '\0';
                ctx->matched_index = 0;

#ifdef DEBUG
                printf("Eingabe: %s\n", ctx->input_buffer);
#endif
                if (ctx->configured && ctx->width > 0) draw_frame(ctx);
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



static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    register struct app_context *ctx = (struct app_context *)data;
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
    }
}



static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}



static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};



void registry_handle_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    register struct app_context *ctx = (struct app_context *)data;

    if (strncmp(interface, "wl_compositor", 13) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, bind_ver);
    } else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        uint32_t bind_ver = (version < 4) ? version : 4;
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, bind_ver);
    } else if (strncmp(interface, "wl_shm", 6) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, bind_ver);
    } else if (strncmp(interface, "wl_seat", 7) == 0) {
        uint32_t bind_ver = (version < 1) ? version : 1;
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, bind_ver);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    }
}



void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}



const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};
