#include "wayland-core.h"
#include "buffer.h"
#include "parser.h"
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

/* 1. KEYMAP-EVENT: Hyprland schickt uns beim Start das exakt konfigurierte System-Layout */
static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)keyboard;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    /* Das vom System gesendete Layout in den XKB-Speicher mappen */
    char *map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    /* Alte Keymap loeschen falls vorhanden, um Leaks zu vermeiden */
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
    printf("wlauncher: Tastatur-Fokus ERHALTEN.\n");
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
    printf("wlauncher: Tastatur-Fokus VERLOREN.\n");
}

/* 2. MODIFIERS-EVENT: Aktualisiert den Shift/AltGr-Status im Speicher */
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

/* 3. KEY-EVENT: JEDER TASTENDRUCK WIRD HIER REINLÄNDISCH ÜBERSETZT */
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    register struct app_context *ctx = (struct app_context *)data;
    (void)serial; (void)time; (void)keyboard;

    if (state == 1) {
        if (key == KEY_ESC) {
            ctx->running = 0;
            return;
        }

        /* NAVIGATION: Pfeiltaste RECHTS springt zum nächsten Vorschlag */
        if (key == KEY_RIGHT && ctx->matched_count > 0) {
            ctx->matched_index = (ctx->matched_index + 1) % ctx->matched_count;
            draw_frame(ctx);
            return;
        }

        /* NAVIGATION: Pfeiltaste LINKS springt zum vorherigen Vorschlag */
        if (key == KEY_LEFT && ctx->matched_count > 0) {
            ctx->matched_index = (ctx->matched_index - 1 + ctx->matched_count) % ctx->matched_count;
            draw_frame(ctx);
            return;
        }

        /* AUSFÜHREN BEI ENTER */
        if (key == KEY_ENTER) {
            char *exec_cmd = NULL;

            /* Wenn Treffer da sind, nimm den aktuell ausgewählten Index! */
            if (ctx->matched_count > 0 && ctx->matched_index < ctx->matched_count) {
                exec_cmd = ctx->matched_apps[ctx->matched_index].exec;
                printf("wlauncher: Starte Vorschlag [%d]: %s via '%s'\n",
                       ctx->matched_index, ctx->matched_apps[ctx->matched_index].name, exec_cmd);
            } else {
                /* Fallback, falls gar nichts matcht: Führe die rohe Eingabe aus */
                exec_cmd = ctx->input_buffer;
            }

            if (exec_cmd && exec_cmd[0] != '\0') {
               pid_t pid = fork();
               if (pid == 0) {
                   setsid();
                   char *args[] = {exec_cmd, NULL};

                   /* KORREKTUR: Der echte POSIX-Systemaufruf */
                   execvp(exec_cmd, args);

                   _exit(1);
                }
            }
            ctx->running = 0;
            return;
        }

        if (key == KEY_BACKSPACE && ctx->input_length > 0) {
            ctx->input_buffer[--ctx->input_length] = '\0';
            ctx->matched_index = 0; /* Reset des Auswahl-Index bei Änderung */
            draw_frame(ctx);
            return;
        }

        /* Die universelle XKB-Eingabe (Auswahl-Index bei jedem neuen Buchstaben nullen!) */
        char utf8_buf[8] = {0};
        if (ctx->xkb_state && xkb_state_key_get_utf8(ctx->xkb_state, key + 8, utf8_buf, sizeof(utf8_buf)) > 0) {
            if (utf8_buf[0] >= 32 && ctx->input_length < 255) {
                ctx->input_buffer[ctx->input_length++] = utf8_buf[0];
                ctx->input_buffer[ctx->input_length] = '\0';

                ctx->matched_index = 0; /* Erster Treffer ist initial fokussiert */
                draw_frame(ctx);
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
    (void)version;
    if (strncmp(interface, "wl_compositor", 13) == 0) {
        ctx->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strncmp(interface, "zwlr_layer_shell_v1", 19) == 0) {
        ctx->layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 4);
    } else if (strncmp(interface, "wl_shm", 6) == 0) {
        ctx->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strncmp(interface, "wl_seat", 7) == 0) {
        ctx->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
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
