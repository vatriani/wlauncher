#define _GNU_SOURCE
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include "parser.h"

void fetch_hyprland_colors(struct app_context *ctx) {
    /* 1. ABSOLUTE STANDARD-FALLBACKS INITIALISIEREN */
    /* (Dein schickes Dunkelgrau und das bewährte Conky-Blau) */
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;

    /* 2. UMGEBUNGS-CHECK: Läuft Hyprland überhaupt? */
    /* Wenn die Instanz-Signatur fehlt, brechen wir sofort ab und nutzen die Standards! */
    if (!getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
        printf("wlauncher: Keine aktive Hyprland-Sitzung erkannt. Nutze Standardfarben.\n");
        return;
    }

    /* 3. IPC-ABFRAGE: Nur ausführen, wenn Hyprland garantiert antworten kann */
    FILE *str = popen("hyprctl getoption general:col.active_border -j 2>/dev/null", "r");
    if (!str) return;

    char buf[256];
    char *hex_start = NULL;

    while (fgets(buf, sizeof(buf), str)) {
        hex_start = strstr(buf, "\"str\": \"");
        if (hex_start) {
            hex_start += 8;
            break;
        }
    }
    pclose(str);

    if (!hex_start) return;

    if (hex_start[0] == '0' && hex_start[1] == 'x') hex_start += 2;
    if (hex_start[0] == '#') hex_start += 1;

    if (strlen(hex_start) >= 6) {
        unsigned int hex_val = 0;
        if (strlen(hex_start) >= 8) {
            sscanf(hex_start + 2, "%6x", &hex_val);
        } else {
            sscanf(hex_start, "%6x", &hex_val);
        }

        ctx->accent_r = ((hex_val >> 16) & 0xFF) / 255.0;
        ctx->accent_g = ((hex_val >> 8) & 0xFF) / 255.0;
        ctx->accent_b = (hex_val & 0xFF) / 255.0;

        /* Der Hintergrund wird ein stark abgedunkeltes Derivat deines Akzents */
        ctx->bg_r = ctx->accent_r * 0.25;
        ctx->bg_g = ctx->accent_g * 0.25;
        ctx->bg_b = ctx->accent_b * 0.25;

        if (ctx->bg_r > 0.15) ctx->bg_r = 0.117;
        if (ctx->bg_g > 0.15) ctx->bg_g = 0.117;
        if (ctx->bg_b > 0.15) ctx->bg_b = 0.15;
    }
}


void scan_applications(struct app_context *ctx) {
    const char *dir_path = "/usr/share/applications";
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    /* FIX: Auf 512 vergrößert, damit Pfad + Dateiname garantiert hineinpassen */
    char file_path[512];
    char line[512];

    ctx->app_count = 0;
    /* Loop durch den Ordner im RAM */
    while ((entry = readdir(dir)) != NULL && ctx->app_count < MAX_APPS) {
        /* Nur Dateien betrachten, die auf .desktop enden */
        if (strstr(entry->d_name, ".desktop") == NULL) continue;

        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        FILE *f = fopen(file_path, "r");
        if (!f) continue;

        struct app_info *app = &ctx->apps[ctx->app_count];
        int has_name = 0, has_exec = 0;

        while (fgets(line, sizeof(line), f)) {
            /* Filtere den Namen heraus */
            if (!has_name && strncmp(line, "Name=", 5) == 0) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';

                /* KORREKTUR: %.255s zwingt snprintf, maximal 255 Zeichen zu lesen -> 0 Warnungen! */
                snprintf(app->name, sizeof(app->name), "%.255s", line + 5);
                has_name = 1;
            }

            /* Filtere den echten Befehl heraus */
            if (!has_exec && strncmp(line, "Exec=", 5) == 0) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';

                /* KORREKTUR: Ebenfalls auf 255 Zeichen begrenzt */
                snprintf(app->exec, sizeof(app->exec), "%.255s", line + 5);

                /* KUGELSICHERER ABSCHNEIDER FÜR ARGUMENTE */
                char *arg = strstr(app->exec, " %");
                if (arg) *arg = '\0';

                arg = strchr(app->exec, '%');
                if (arg) *arg = '\0';

                /* Entfernt eventuelle Anführungszeichen (z.B. bei Flatpaks) */
                /* KORREKTUR: Wir prüfen das ERSTE Zeichen [0] des Arrays, nicht den Pointer! */
                if (app->exec[0] == '"' || app->exec[0] == '\'') {
                    char quote_char = app->exec[0]; // Sichert das gefundene Zeichen ('"' oder '\'')

                    /* Verschiebt den restlichen String um 1 nach links, um das erste Anführungszeichen zu killen */
                    memmove(app->exec, app->exec + 1, strlen(app->exec));

                    /* KORREKTUR: Sucht nach dem passenden schließenden Zeichen (quote_char) als int */
                    char *end = strchr(app->exec, quote_char);
                    if (end) *end = '\0';
                }

                has_exec = 1;
            }

            if (has_name && has_exec) {
                ctx->app_count++;
                break;
            }
        }
        fclose(f);
    }
    closedir(dir);
    printf("wlauncher: %d Anwendungen erfolgreich im RAM indiziert.\n", ctx->app_count);
}

void find_best_matches(struct app_context *ctx, const char *search) {
    ctx->matched_count = 0;
    if (!search || search[0] == '\0') return;

    /* Loop durch alle indizierten Programme im RAM */
    for (int i = 0; i < ctx->app_count; ++i) {
        if (strcasestr(ctx->apps[i].name, search) != NULL ||
            strcasestr(ctx->apps[i].exec, search) != NULL) {

            /* Kopiere den Treffer in unser Top-5-Array */
            int idx = ctx->matched_count;
            strncpy(ctx->matched_apps[idx].name, ctx->apps[i].name, 255);
            strncpy(ctx->matched_apps[idx].exec, ctx->apps[i].exec, 255);

            ctx->matched_count++;

            /* Wenn wir 5 Treffer beisammen haben, brechen wir ab */
            if (ctx->matched_count >= 5) break;
        }
    }
}
