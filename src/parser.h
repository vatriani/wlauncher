#ifndef PARSER_H
#define PARSER_H
#define _GNU_SOURCE

#include "types.h"
#include "basics.h"

typedef enum {
    SCAN_ALL    = 0,
    SCAN_APPEND = (1 << 0) ///< scan only append apps
} scan;


void scan_applications(struct app_context *ctx, scan tmp);
void find_best_matches(struct app_context *ctx, const char *search);

#endif
