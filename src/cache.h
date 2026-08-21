#ifndef CACHE_H
#define CACHE_H

#include "types.h"

/* return: 1 = loaded, 0 = cache missing/invalid, -1 = hard error */
int cache_load_if_valid(struct app_context *ctx);
int cache_store(const struct app_context *ctx);

#endif
