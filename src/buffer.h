#ifndef BUFFER_H
#define BUFFER_H

#include "types.h"



int setupCairo(struct app_context *ctx) ;
void draw_frame(struct app_context *ctx);
void cairo_cleanup(struct app_context *ctx);
color rgb_to_double(char *tmp);


#endif
