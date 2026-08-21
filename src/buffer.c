#define _GNU_SOURCE

#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include "buffer.h"
#include "parser.h"
#include "types.h"



static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wlauncher-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * @brief Implements a buffer release event.
 *
 * Actually not needed by the programm but needed for wayland connections.
 */
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct app_context *ctx = data;
    if (!ctx) return;
    if (ctx->render.buffer == wl_buffer) {
        ctx->render.buffer_busy = 0;  // wiederverwendbar
    }
}

/**
 * @brief Implements a buffer release listener.
 *
 * Actually not needed by the programm but needed for wayland connections.
 */
static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};


int setupCairo(struct app_context *ctx) {
    ctx->render.fhm_stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, ctx->render.width);
    ctx->render.fhm_size = ctx->render.fhm_stride * ctx->render.height;

    ctx->render.fhm_fd = allocate_shm_file(ctx->render.fhm_size);
    if (ctx->render.fhm_fd < 0) return -1;

    ctx->render.fhm_data = mmap(NULL, ctx->render.fhm_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, ctx->render.fhm_fd, 0);
    if (ctx->render.fhm_data == MAP_FAILED) {
        ctx->render.fhm_data = NULL;
        close(ctx->render.fhm_fd);
        ctx->render.fhm_fd = -1;
        return -1;
    }

    memset(ctx->render.fhm_data, 0, ctx->render.fhm_size);

    ctx->render.cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)ctx->render.fhm_data,
        CAIRO_FORMAT_RGB24,
        ctx->render.width,
        ctx->render.height,
        ctx->render.fhm_stride   // <- wichtig
    );
    ctx->render.cr = cairo_create(ctx->render.cairo_surface);

    ctx->render.pango_layout = pango_cairo_create_layout(ctx->render.cr);
    ctx->render.pango_font_desc = pango_font_description_from_string(ctx->render.font);
    pango_layout_set_font_description(ctx->render.pango_layout, ctx->render.pango_font_desc);

    ctx->render.pool = wl_shm_create_pool(ctx->render.wl_shm, ctx->render.fhm_fd, ctx->render.fhm_size);
    ctx->render.buffer = wl_shm_pool_create_buffer(ctx->render.pool, 0,
        ctx->render.width, ctx->render.height, ctx->render.fhm_stride, WL_SHM_FORMAT_XRGB8888);

    if (!ctx->render.buffer) {
        wl_shm_pool_destroy(ctx->render.pool);
        munmap(ctx->render.fhm_data, ctx->render.fhm_size);
        ctx->render.fhm_data = NULL;
        close(ctx->render.fhm_fd);
        ctx->render.fhm_fd = -1;
        return -1;
    }

    wl_buffer_add_listener(ctx->render.buffer, &buffer_listener, ctx);
    ctx->configured = 1;
    return 0;
}

void cairo_cleanup(struct app_context *ctx) {
    if (ctx->render.pango_font_desc) pango_font_description_free(ctx->render.pango_font_desc);
    if (ctx->render.pango_layout) g_object_unref(ctx->render.pango_layout);
    if (ctx->render.buffer) wl_buffer_destroy(ctx->render.buffer);
    if (ctx->render.fhm_data) munmap(ctx->render.fhm_data, ctx->render.fhm_size);
    if (ctx->render.fhm_fd >= 0) close(ctx->render.fhm_fd);
    cairo_destroy(ctx->render.cr);
    cairo_surface_destroy(ctx->render.cairo_surface);
}



static void set_source_rgb(cairo_t *cr, color *tmp_color) {
    cairo_set_source_rgb(cr, tmp_color->r, tmp_color->g, tmp_color->b);
}



void draw_frame(struct app_context *ctx) {
    if (ctx->render.width <= 0 || ctx->render.height <= 0 || !ctx->configured) return;
    if (!ctx->render.buffer) return;
    if (ctx->render.buffer_busy) return; // warten bis release-event kam

    set_source_rgb(ctx->render.cr, &ctx->render.bg_color);
    cairo_paint(ctx->render.cr);

    set_source_rgb(ctx->render.cr, &ctx->render.accent_color);
    cairo_rectangle(ctx->render.cr, 0, 0, 100, ctx->render.height);
    cairo_fill(ctx->render.cr);

    pango_layout_set_text(ctx->render.pango_layout, APP_NAME, -1);
    set_source_rgb(ctx->render.cr, &ctx->render.bg_color);
    cairo_move_to(ctx->render.cr, 10, 3);
    pango_cairo_show_layout(ctx->render.cr, ctx->render.pango_layout);

    int current_offset = 115;

    if (ctx->input_length > 0) {
        pango_layout_set_text(ctx->render.pango_layout, ctx->input_buffer, -1);
        set_source_rgb(ctx->render.cr, &ctx->render.fg_color);
        cairo_move_to(ctx->render.cr, current_offset, 3);
        pango_cairo_show_layout(ctx->render.cr, ctx->render.pango_layout);

        int input_width, input_height;
        pango_layout_get_pixel_size(ctx->render.pango_layout, &input_width, &input_height);
        current_offset += input_width + 20;
    }

    for (register int i = 0; i < ctx->matched_count; ++i) {
        char display_text[260];
        snprintf(display_text, sizeof(display_text), " %s ", ctx->matched_apps[i]->name);
        pango_layout_set_text(ctx->render.pango_layout, display_text, -1);


        int item_width, item_height;
        pango_layout_get_pixel_size(ctx->render.pango_layout, &item_width, &item_height);

        if (current_offset + item_width > ctx->render.width) break;

        if (i == ctx->matched_index) {
            set_source_rgb(ctx->render.cr, &ctx->render.accent_color);
            cairo_rectangle(ctx->render.cr, current_offset, 0, item_width, ctx->render.height);
            cairo_fill(ctx->render.cr);
            set_source_rgb(ctx->render.cr, &ctx->render.bg_color);
        } else {
            set_source_rgb(ctx->render.cr, &ctx->render.fg_color);
        }

        cairo_move_to(ctx->render.cr, current_offset, 3);
        pango_cairo_show_layout(ctx->render.cr, ctx->render.pango_layout);
        current_offset += item_width + 15;
    }
    cairo_surface_flush(ctx->render.cairo_surface);
    wl_surface_attach(ctx->render.surface, ctx->render.buffer, 0, 0);
    wl_surface_damage_buffer(ctx->render.surface, 0, 0, ctx->render.width, ctx->render.height);
    wl_surface_commit(ctx->render.surface);
    ctx->render.buffer_busy = 1;
    fprintf(stderr, "draw_frame: %dx%d buffer=%p surface=%p\n",
        ctx->render.width, ctx->render.height,
        (void*)ctx->render.buffer, (void*)ctx->render.surface);
}

color rgb_to_double(char *tmp) {
    unsigned int hex_val = 0;
    char hex_tmp[7] = {0};
    color ret;

    if (tmp[0] == '#') strncpy(hex_tmp, tmp+1, 6);
    else strncpy(hex_tmp, tmp, 6);

    if (sscanf(hex_tmp, "%x", &hex_val) == 1) {
        ret.r = ((hex_val >> 16) & 0xFF) / 255.0;
        ret.g = ((hex_val >> 8) & 0xFF) / 255.0;
        ret.b = (hex_val & 0xFF) / 255.0;
    }
    return ret;
}
