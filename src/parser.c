#define _GNU_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <ctype.h>

#include "parser.h"



static int is_duplicate_app(const struct app_context *ctx, const char *name, const char *exec) {
    for (register int i = 0; i < ctx->app_count; ++i)
        if (strcasecmp(ctx->apps[i].name, name) == 0 || strcasecmp(ctx->apps[i].exec, exec) == 0)
            return 1;
    return 0;
}



void scan_applications(struct app_context *ctx) {
    const char *base_paths[] = {
        "/.local/share/applications",   // user-path
        "/usr/share/applications",      // system-path
        "/usr/local/share/applications" // local Admin-path
    };
    int path_count = sizeof(base_paths) / sizeof(base_paths[0]);

    struct dirent *entry;

    char resolved_path[512];
    char file_path[1024];
    char line[512];

    ctx->app_count = 0;
    char *home_dir = getenv("HOME");

    for (int p_idx = 0; p_idx < path_count; ++p_idx) {
        if (p_idx == 0) {
            if (!home_dir) continue;
            snprintf(resolved_path, sizeof(resolved_path), "%s%s", home_dir, base_paths[p_idx]);
        } else {
            snprintf(resolved_path, sizeof(resolved_path), "%s", base_paths[p_idx]);
        }

        DIR *dir = opendir(resolved_path);
        if (!dir) continue;

        while ((entry = readdir(dir)) != NULL && ctx->app_count < MAX_APPS) {
            if (strstr(entry->d_name, ".desktop") == NULL) continue;

            snprintf(file_path, sizeof(file_path), "%s/%s", resolved_path, entry->d_name);
            FILE *f = fopen(file_path, "r");
            if (!f) continue;

            char temp_name[256] = {0};
            char temp_exec[256] = {0};
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

                    snprintf(temp_name, sizeof(temp_name), "%.255s", line + 5);
                    has_name = 1;
                }

                if (!has_exec && strncmp(line, "Exec=", 5) == 0) {
                    char *newline = strchr(line, '\n');
                    if (newline) *newline = '\0';

                    snprintf(temp_exec, sizeof(temp_exec), "%.255s", line + 5);

                    char *p = temp_exec;
                    while ((p = strchr(p, '%')) != NULL) {
                        if (p[1] == 'u' || p[1] == 'U' || p[1] == 'f' || p[1] == 'F' ||
                            p[1] == 'i' || p[1] == 'c' || p[1] == 'k') {
                            p[0] = ' ';
                            p[1] = ' ';
                        }
                        p++;
                    }

                    if (temp_exec[0] == '"' || temp_exec[0] == '\'') {
                        char quote_char = temp_exec[0];
                        memmove(temp_exec, temp_exec + 1, strlen(temp_exec));
                        char *end = strchr(temp_exec, quote_char);
                        if (end) *end = '\0';
                    }
                    has_exec = 1;
                }
            }
            fclose(f);

            if (has_name && has_exec && strlen(temp_name) > 0 && strlen(temp_exec) > 0) {
                if (!is_duplicate_app(ctx, temp_name, temp_exec)) {
                    struct app_info *app = &ctx->apps[ctx->app_count];

                    snprintf(app->name, sizeof(app->name), "%s", temp_name);
                    snprintf(app->exec, sizeof(app->exec), "%s", temp_exec);

                    ++ctx->app_count;
                }
            }
        }
        closedir(dir);
    }

#ifdef DEBUG
    printf("wlauncher: %d valide Anwendungen (XDG-übergreifend) im RAM indiziert.\n", ctx->app_count);
#endif
}



int get_fuzzy_score(const char *str, const char *search) {
    if (!search || *search == '\0' || !str || *str == '\0') return -1;

    char *exact_match = strcasestr(str, search);
    if (exact_match) {
        return (int)(exact_match - str);
    }

    const char *s = search;
    const char *p = str;

    int score = 2000;
    int matches = 0;
    int search_len = strlen(search);

    while (*s != '\0') {
        char s_char = tolower((unsigned char)*s);
        const char *p_look = p;
        int distance = 0;
        int found = 0;

        while (*p_look != '\0' && distance < 4) {
            if (tolower((unsigned char)*p_look) == s_char) {
                found = 1;
                break;
            }
            distance++;
            p_look++;
        }

        if (found) {
            matches++;
            score += distance * 10;
            p = p_look + 1;
        } else score += 100;
        ++s;
    }

    int required_matches = (search_len > 3) ? (search_len / 2) : 1;

    if (matches >= required_matches) {
        return score;
    }

    return -1;
}



int compare_cached_entries(const void *a, const void *b) {
    const struct sorted_entry *entryA = (const struct sorted_entry *)a;
    const struct sorted_entry *entryB = (const struct sorted_entry *)b;

    if (entryA->score != entryB->score) return entryA->score - entryB->score;

    if (entryA->app < entryB->app) return -1;
    if (entryA->app > entryB->app) return 1;
    return 0;
}



void find_best_matches(struct app_context *ctx, const char *search) {
    ctx->matched_count = 0;

    struct sorted_entry temp_entries[MAX_APPS];
    int temp_count = 0;

    if (!search || search[0] == '\0') {
        ctx->matched_count = (ctx->app_count > MAX_MATCHED_APPS) ? MAX_MATCHED_APPS : ctx->app_count;

        for (int i = 0; i < ctx->app_count; ++i) {
            temp_entries[i].app = &ctx->apps[i];
            temp_entries[i].score = 0;
        }

        if (ctx->app_count > 1) {
            qsort(temp_entries, ctx->app_count, sizeof(struct sorted_entry), compare_cached_entries);
        }

        ctx->matched_count = (ctx->app_count > MAX_MATCHED_APPS) ? MAX_MATCHED_APPS : ctx->app_count;
    for (int i = 0; i < ctx->matched_count; ++i) {
        ctx->matched_apps[i] = temp_entries[i].app;
    }
    return;
    }

    for (int i = 0; i < ctx->app_count; ++i) {
        int name_score = get_fuzzy_score(ctx->apps[i].name, search);
        int exec_score = get_fuzzy_score(ctx->apps[i].exec, search);

        if (name_score != -1 || exec_score != -1) {
            int final_score = 100000;

            if (name_score != -1) final_score = name_score;
            else if (exec_score != -1) final_score = exec_score + 5000;

            temp_entries[temp_count].app = &ctx->apps[i];
            temp_entries[temp_count].score = final_score;
            temp_count++;

            if (temp_count >= MAX_APPS) break;
        }
    }

    if (temp_count > 1 && search && search[0] != '\0')
        qsort(temp_entries, temp_count, sizeof(struct sorted_entry), compare_cached_entries);

    ctx->matched_count = (temp_count > MAX_MATCHED_APPS) ? MAX_MATCHED_APPS : temp_count;
      for (int i = 0; i < ctx->matched_count; ++i)
          ctx->matched_apps[i] = temp_entries[i].app;

}
