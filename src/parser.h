#ifndef PARSER_H
#define PARSER_H

#include "types.h"

void fetch_hyprland_colors(struct app_context *ctx);
void scan_applications(struct app_context *ctx);
void find_best_matches(struct app_context *ctx, const char *search);

#endif
