#define _GNU_SOURCE
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include "parser.h"

void fetch_hyprland_colors(struct app_context *ctx) {
    /* Standard-Fallbacks (dein bewährtes Dunkelgrau und Conky-Blau) */
    ctx->bg_r = 0.117; ctx->bg_g = 0.117; ctx->bg_b = 0.180;
    ctx->accent_r = 0.321; ctx->accent_g = 0.443; ctx->accent_b = 0.654;

    /* Wir rufen hyprctl via Pipe auf, um die aktive Rahmenfarbe abzufragen */
    FILE *str = popen("hyprctl getoption general:col.active_border -j 2>/dev/null", "r");
    if (!str) return;

    char buf[1024];
    char *hex_start = NULL;

    /* Wir durchsuchen den JSON-Output nach dem Feld "str": "rgba(...)" oder "0x..." */
    while (fgets(buf, sizeof(buf), str)) {
        hex_start = strstr(buf, "\"str\": \"");
        if (hex_start) {
            hex_start += 8; /* Pointer auf den Anfang des Hex-Werts schieben */
            break;
        }
    }
    pclose(str);

    if (!hex_start) return;

    /* Überspringt '0x' oder '#' falls Hyprland das im String liefert */
    if (hex_start[0] == '0' && hex_start[1] == 'x') hex_start += 2;
    if (hex_start[0] == '#') hex_start += 1;

    /* Wenn der String lang genug ist (AARRGGBB Format), extrahieren wir die RGB-Werte */
    if (strlen(hex_start) >= 6) {
        unsigned int hex_val = 0;
        /* Wenn Alpha mitgeliefert wird (8 Stellen), überspringen wir die ersten beiden Stellen für RGB */
        if (strlen(hex_start) >= 8) {
            sscanf(hex_start + 2, "%6x", &hex_val);
        } else {
            sscanf(hex_start, "%6x", &hex_val);
        }

        /* Mathematische Bit-Shifts für die RGB-Kanäle (0-255 -> 0.0-1.0) */
        ctx->accent_r = ((hex_val >> 16) & 0xFF) / 255.0;
        ctx->accent_g = ((hex_val >> 8) & 0xFF) / 255.0;
        ctx->accent_b = (hex_val & 0xFF) / 255.0;

        /* Der Hintergrund wird automatisch ein stark abgedunkeltes Derivat deines Akzents (Harmonie-Look) */
        ctx->bg_r = ctx->accent_r * 0.25;
        ctx->bg_g = ctx->accent_g * 0.25;
        ctx->bg_b = ctx->accent_b * 0.25;

        /* Sicherheitsnetz: Falls der Hintergrund zu hell wird, deckeln wir ihn */
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
    char file_path[256];
    char line[256];

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
                strncpy(app->name, line + 5, sizeof(app->name) - 1);
                has_name = 1;
            }
            /* Filtere den echten Befehl heraus */
            if (!has_exec && strncmp(line, "Exec=", 5) == 0) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';

                /* Kopiert den rohen Befehl ab Zeichen 5 */
                strncpy(app->exec, line + 5, sizeof(app->exec) - 1);

                /* KUGELSICHERER ABSCHNEIDER FÜR ALLES WAS NACH DEM BEFEHL KOMMT */
                /* Wir schneiden bei jedem Leerzeichen ab, das ein Prozentzeichen einleitet */
                char *arg = strstr(app->exec, " %");
                if (arg) *arg = '\0';

                /* Sicherheitsnetz für Argumente ohne Leerzeichen */
                arg = strchr(app->exec, '%');
                if (arg) *arg = '\0';

                /* Entfernt eventuelle Anführungszeichen (wichtig bei manchen Flatpaks/KDE-Apps) */
                if (app->exec[0] == '"' || app->exec[0] == '\'') {
                    memmove(app->exec, app->exec + 1, strlen(app->exec));
                    char *end = strchr(app->exec, app->exec[0]);
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
