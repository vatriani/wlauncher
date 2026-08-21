#define _GNU_SOURCE
#include "cache.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define CACHE_MAGIC "WLAC"
#define CACHE_VERSION 1
#define CACHE_DIR_COUNT 3

static const char *base_paths[CACHE_DIR_COUNT] = {
    "/.local/share/applications",
    "/usr/share/applications",
    "/usr/local/share/applications"
};

typedef struct cache_dir_fp_t {
    uint64_t dev;
    uint64_t ino;
    int64_t  mtime_sec;
    int64_t  mtime_nsec;
} cache_dir_fp;

typedef struct cache_header_t {
    char     magic[4];
    uint32_t version;
    uint32_t dir_count;
    uint32_t app_count;
} cache_header;

typedef struct cache_app_record_t {
    char name[MAX_NAME_LENGTH];
    char exec[MAX_NAME_LENGTH];
} cache_app_record;

static int get_cache_file_path(char *out, size_t out_sz) {
    const char *xdg_cache = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    char dir[PATH_MAX];

    if (xdg_cache && xdg_cache[0] != '\0') {
        if (snprintf(dir, sizeof(dir), "%s/%s", xdg_cache, APP_NAME) >= (int)sizeof(dir))
            return -1;
    } else {
        if (!home || home[0] == '\0') return -1;
        if (snprintf(dir, sizeof(dir), "%s/.cache/%s", home, APP_NAME) >= (int)sizeof(dir))
            return -1;
    }

    if (mkdir(dir, 0700) < 0 && errno != EEXIST) return -1;

    if (snprintf(out, out_sz, "%s/apps.cache", dir) >= (int)out_sz)
        return -1;

    return 0;
}

static int resolve_scan_path(int idx, char *out, size_t out_sz) {
    if (idx < 0 || idx >= CACHE_DIR_COUNT) return -1;
    if (idx == 0) {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') return -1;
        if (snprintf(out, out_sz, "%s%s", home, base_paths[idx]) >= (int)out_sz) return -1;
    } else {
        if (snprintf(out, out_sz, "%s", base_paths[idx]) >= (int)out_sz) return -1;
    }
    return 0;
}

static void fill_fp_for_missing(cache_dir_fp *fp) {
    fp->dev = 0;
    fp->ino = 0;
    fp->mtime_sec = 0;
    fp->mtime_nsec = 0;
}

static int collect_current_fingerprints(cache_dir_fp out[CACHE_DIR_COUNT]) {
    for (int i = 0; i < CACHE_DIR_COUNT; ++i) {
        char path[PATH_MAX];
        struct stat st;

        if (resolve_scan_path(i, path, sizeof(path)) != 0) {
            fill_fp_for_missing(&out[i]);
            continue;
        }

        if (stat(path, &st) != 0) {
            fill_fp_for_missing(&out[i]);
            continue;
        }

        out[i].dev = (uint64_t)st.st_dev;
        out[i].ino = (uint64_t)st.st_ino;
        out[i].mtime_sec = (int64_t)st.st_mtim.tv_sec;
        out[i].mtime_nsec = (int64_t)st.st_mtim.tv_nsec;
    }
    return 0;
}

static int fp_equal(const cache_dir_fp *a, const cache_dir_fp *b) {
    return a->dev == b->dev &&
           a->ino == b->ino &&
           a->mtime_sec == b->mtime_sec &&
           a->mtime_nsec == b->mtime_nsec;
}

static void clear_apps_vector(struct app_context *ctx) {
    int total = ctx->apps.pfVectorTotal(&ctx->apps);
    for (int i = 0; i < total; ++i) {
        app_info *app = (app_info *)ctx->apps.pfVectorGet(&ctx->apps, i);
        free(app);
    }
    ctx->apps.pfVectorFree(&ctx->apps);
    vector_init(&ctx->apps);
}

int cache_load_if_valid(struct app_context *ctx) {
    if (!ctx) return -1;

    char cache_path[PATH_MAX];
    if (get_cache_file_path(cache_path, sizeof(cache_path)) != 0) return 0;

    FILE *f = fopen(cache_path, "rb");
    if (!f) return 0;

    cache_header hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
        fclose(f);
        return 0;
    }

    if (memcmp(hdr.magic, CACHE_MAGIC, 4) != 0 ||
        hdr.version != CACHE_VERSION ||
        hdr.dir_count != CACHE_DIR_COUNT) {
        fclose(f);
        return 0;
    }

    cache_dir_fp file_fp[CACHE_DIR_COUNT];
    if (fread(file_fp, sizeof(cache_dir_fp), CACHE_DIR_COUNT, f) != CACHE_DIR_COUNT) {
        fclose(f);
        return 0;
    }

    cache_dir_fp current_fp[CACHE_DIR_COUNT];
    collect_current_fingerprints(current_fp);

    for (int i = 0; i < CACHE_DIR_COUNT; ++i) {
        if (!fp_equal(&file_fp[i], &current_fp[i])) {
            fclose(f);
            return 0;
        }
    }

    clear_apps_vector(ctx);

    for (uint32_t i = 0; i < hdr.app_count; ++i) {
        cache_app_record rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1) {
            fclose(f);
            clear_apps_vector(ctx);
            return -1;
        }

        app_info *app = calloc(1, sizeof(app_info));
        if (!app) {
            fclose(f);
            clear_apps_vector(ctx);
            return -1;
        }

        memcpy(app->name, rec.name, sizeof(app->name));
        memcpy(app->exec, rec.exec, sizeof(app->exec));
        app->name[MAX_NAME_LENGTH - 1] = '\0';
        app->exec[MAX_NAME_LENGTH - 1] = '\0';

        if (ctx->apps.pfVectorAdd(&ctx->apps, app) != 0) {
            free(app);
            fclose(f);
            clear_apps_vector(ctx);
            return -1;
        }
    }

    fclose(f);
    return 1;
}

int cache_store(const struct app_context *ctx) {
    if (!ctx) return -1;

    char cache_path[PATH_MAX];
    if (get_cache_file_path(cache_path, sizeof(cache_path)) != 0) return -1;

    char tmp_path[PATH_MAX];
    if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", cache_path) >= (int)sizeof(tmp_path))
        return -1;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return -1;

    cache_header hdr;
    memcpy(hdr.magic, CACHE_MAGIC, 4);
    hdr.version = CACHE_VERSION;
    hdr.dir_count = CACHE_DIR_COUNT;
    hdr.app_count = (uint32_t)ctx->apps.pfVectorTotal((vector *)&ctx->apps);

    cache_dir_fp fp[CACHE_DIR_COUNT];
    collect_current_fingerprints(fp);

    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1 ||
        fwrite(fp, sizeof(cache_dir_fp), CACHE_DIR_COUNT, f) != CACHE_DIR_COUNT) {
        fclose(f);
        unlink(tmp_path);
        return -1;
    }

    int total = ctx->apps.pfVectorTotal((vector *)&ctx->apps);
    for (int i = 0; i < total; ++i) {
        app_info *app = (app_info *)ctx->apps.pfVectorGet((vector *)&ctx->apps, i);
        if (!app) continue;

        cache_app_record rec;
        memset(&rec, 0, sizeof(rec));
        memcpy(rec.name, app->name, sizeof(rec.name));
        memcpy(rec.exec, app->exec, sizeof(rec.exec));

        if (fwrite(&rec, sizeof(rec), 1, f) != 1) {
            fclose(f);
            unlink(tmp_path);
            return -1;
        }
    }

    if (fflush(f) != 0) {
        fclose(f);
        unlink(tmp_path);
        return -1;
    }

    if (fsync(fileno(f)) != 0) {
        fclose(f);
        unlink(tmp_path);
        return -1;
    }

    if (fclose(f) != 0) {
        unlink(tmp_path);
        return -1;
    }

    if (rename(tmp_path, cache_path) != 0) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}
