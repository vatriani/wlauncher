#define _GNU_SOURCE

#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

/* Die Grafik-Header einbinden */
#include <cairo.h>
#include <pango/pangocairo.h>

#include "buffer.h"
#include "parser.h"

static int allocate_shm_file(size_t size) {
    int fd = memfd_create("wlauncher-shared-buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void draw_frame(struct app_context *ctx) {
    int stride = ctx->width * 4;
    int size = stride * ctx->height;

    int fd = allocate_shm_file(size);
    if (fd < 0) return;

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return;
    }

    /* 1. CAIRO INITIALISIEREN: Zeichnet direkt auf unserem Shared-Memory Pointer! */
    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        (unsigned char *)data, CAIRO_FORMAT_ARGB32, ctx->width, ctx->height, stride
    );
    cairo_t *cr = cairo_create(cairo_surface);

    /* 2. HINTERGRUND FÄRBEN: Übernimmt die dynamische Hintergrundfarbe */
    cairo_set_source_rgb(cr, ctx->bg_r, ctx->bg_g, ctx->bg_b);
    cairo_paint(cr);

    /* 3. DMENU-PROMPT ZEICHNEN: Übernimmt deine exakte Hyprland-Rahmenfarbe! */
    cairo_set_source_rgb(cr, ctx->accent_r, ctx->accent_g, ctx->accent_b);
    cairo_rectangle(cr, 0, 0, 100, ctx->height);
    cairo_fill(cr);

    /* Text für den Prompt via Pango auf den blauen Block zeichnen */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, " wlauncher >", -1);
    PangoFontDescription *font_desc = pango_font_description_from_string("DejaVu Sans Mono Bold 11");
    pango_layout_set_font_description(layout, font_desc);

    cairo_set_source_rgb(cr, 0.066, 0.066, 0.105); /* Dunkler Text auf blauem Grund */
    cairo_move_to(cr, 5, 3);
    pango_cairo_show_layout(cr, layout);

    /* 4. LIVE-TASTATUR-EINGABE ZEICHNEN */
   if (ctx->input_length > 0) {
       /* Sucht die aktuellen Top 5 Treffer im RAM zusammen */
       find_best_matches(ctx, ctx->input_buffer);

       /* Deine aktuelle Eingabe in hellem Weiß zeichnen */
       pango_layout_set_text(layout, ctx->input_buffer, -1);
       font_desc = pango_font_description_from_string("DejaVu Sans Mono 11");
       pango_layout_set_font_description(layout, font_desc);

       cairo_set_source_rgb(cr, 0.917, 0.933, 0.956); /* #eaeaea */
       cairo_move_to(cr, 115, 3);
       pango_cairo_show_layout(cr, layout);

       /* Breite deiner Eingabe messen, damit die Vorschläge exakt dahinter ansetzen */
       int input_width, input_height;
       pango_layout_get_pixel_size(layout, &input_width, &input_height);

       int current_offset = 115 + input_width + 20;

       /* DIE TOP 5 NEBENEINANDER RENDERN */
       for (int i = 0; i < ctx->matched_count; ++i) {
           char display_text[280];
           snprintf(display_text, sizeof(display_text), " %s ", ctx->matched_apps[i].name);
           pango_layout_set_text(layout, display_text, -1);

           int item_width, item_height;
           pango_layout_get_pixel_size(layout, &item_width, &item_height);

           /* Wenn dieser Treffer aktiv ausgewählt ist, zeichnen wir einen blauen Hintergrundblock */
           if (i == ctx->matched_index) {
                cairo_set_source_rgb(cr, ctx->accent_r, ctx->accent_g, ctx->accent_b);
                cairo_rectangle(cr, current_offset, 0, item_width, ctx->height);
                cairo_fill(cr);

                cairo_set_source_rgb(cr, ctx->bg_r, ctx->bg_g, ctx->bg_b); /* Invertierter Text */
            } else {
                cairo_set_source_rgb(cr, 0.545, 0.556, 0.639); /* Inaktive Treffer bleiben grau */
            }

           cairo_move_to(cr, current_offset, 3);
           pango_cairo_show_layout(cr, layout);

           /* Offset für den nächsten Eintrag weiterschieben (+ Abstand) */
           current_offset += item_width + 15;
       }
   }

    /* Gtk/Cairo-Speicher sauber freigeben */
    pango_font_description_free(font_desc);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(cairo_surface);

    /* 5. WAYLAND-BUFFER ABSCHICKEN */
    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    ctx->buffer = wl_shm_pool_create_buffer(pool, 0, ctx->width, ctx->height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(ctx->surface, ctx->buffer, 0, 0);
    wl_surface_damage(ctx->surface, 0, 0, ctx->width, ctx->height);
    wl_surface_commit(ctx->surface);

    munmap(data, size);
}
