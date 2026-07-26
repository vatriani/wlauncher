#define _GNU_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include "parser.h"
#include "types.h"



void scan_applications(struct app_context *ctx) {
    const char *dir_path = "/usr/share/applications";
    struct dirent *entry;
    char file_path[512];
    char line[512];

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    ctx->app_count = 0;

    while ((entry = readdir(dir)) != NULL && ctx->app_count < MAX_APPS) {
        if (strstr(entry->d_name, ".desktop") == NULL) continue;

        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        FILE *f = fopen(file_path, "r");
        if (!f) continue;

        struct app_info *app = &ctx->apps[ctx->app_count];
        int has_name = 0, has_exec = 0;

        while (fgets(line, sizeof(line), f)) {
            size_t line_len = strlen(line);

            if (line_len >= sizeof(line) - 1 && line[line_len - 1] != '\n') {
                int ch;
                while ((ch = fgetc(f)) != '\n' && ch != EOF);
                continue;
            }

            if (!has_name && strncmp(line, "Name=", 5) == 0) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';

                snprintf(app->name, sizeof(app->name), "%.255s", line + 5);
                has_name = 1;
            }

            if (!has_exec && strncmp(line, "Exec=", 5) == 0) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';

                snprintf(app->exec, sizeof(app->exec), "%.255s", line + 5);

                char *arg = strstr(app->exec, " %");
                if (arg) *arg = '\0';
                arg = strchr(app->exec, '%');
                if (arg) *arg = '\0';

                if (app->exec[0] == '"' || app->exec[0] == '\'') {
                    char quote_char = app->exec[0];
                    memmove(app->exec, app->exec + 1, strlen(app->exec));
                    char *end = strchr(app->exec, quote_char);
                    if (end) *end = '\0';
                }
                has_exec = 1;
            }
        }
        fclose(f);

        if (has_name && has_exec && strlen(app->name) > 0 && strlen(app->exec) > 0) {
            ++ctx->app_count;
        } else {
            memset(app, 0, sizeof(struct app_info));
        }
    }
    closedir(dir);
#ifdef DEBUG
    printf("wlauncher: %d valide Anwendungen erfolgreich im RAM indiziert.\n", ctx->app_count);
#endif
}



void find_best_matches(struct app_context *ctx, const char *search) {
    ctx->matched_count = 0;
    if (!search || search[0] == '\0') return;

    for (register int iterator = 0; iterator < ctx->app_count; ++iterator) {
        if (strcasestr(ctx->apps[iterator].name, search) != NULL ||
                strcasestr(ctx->apps[iterator].exec, search) != NULL) {

            int idx = ctx->matched_count;

            snprintf(ctx->matched_apps[idx].name,
                    sizeof(ctx->matched_apps[idx].name),
                    "%s", ctx->apps[iterator].name);

            snprintf(ctx->matched_apps[idx].exec,
                    sizeof(ctx->matched_apps[idx].exec),
                    "%s", ctx->apps[iterator].exec);

            ++ctx->matched_count;
            if (ctx->matched_count >= MAX_MATCHED_APPS) break;
        }
    }
}



void fetch_hyprland_colors(struct app_context *ctx) {
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;

    char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return;

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/hypr/%s/.socket.sock", sig);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    const char *cmd = "[[j]]/getoption general:col.active_border";
    if (write(sock, cmd, strlen(cmd)) < 0) { close(sock); return; }

    char buf[1024];
    int len = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    if (len <= 0) return;
    buf[len] = '\0';

    register char *hex_start = strstr(buf, "\"str\": \"");
    if (!hex_start) return;
    hex_start += 8;

    if (*hex_start == '0' && *(hex_start + 1) == 'x') hex_start += 2;
    if (*hex_start == '#') hex_start += 1;

    if (strlen(hex_start) >= 6) {
        unsigned int hex_val = 0;
        if (strlen(hex_start) >= 8) sscanf(hex_start + 2, "%6x", &hex_val);
        else sscanf(hex_start, "%6x", &hex_val);

        ctx->accent_r = ((hex_val >> 16) & 0xFF) / 255.0;
        ctx->accent_g = ((hex_val >> 8) & 0xFF) / 255.0;
        ctx->accent_b = (hex_val & 0xFF) / 255.0;

        ctx->bg_r = ctx->accent_r * 0.25; ctx->bg_g = ctx->accent_g * 0.25; ctx->bg_b = ctx->accent_b * 0.25;
        if (ctx->bg_r > 0.15) ctx->bg_r = 0.117;
        if (ctx->bg_g > 0.15) ctx->bg_g = 0.117;
        if (ctx->bg_b > 0.15) ctx->bg_b = 0.15;
    }
}
