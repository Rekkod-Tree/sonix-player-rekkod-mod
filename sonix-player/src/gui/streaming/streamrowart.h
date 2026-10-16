#ifndef STREAMROWART_H
#define STREAMROWART_H

#include "lvgl/lvgl.h"

// GUI thread. A fixed row keeps its small picture until recycled. Only visible
// rows are fetched, on a separate shared worker; failures keep the colour.
void streamrowart_bind(lv_obj_t *owner, lv_obj_t *row, int index, const char *url, lv_color_t tone);
void streamrowart_forget(lv_obj_t *row);
void streamrowart_visible(lv_obj_t *owner, int first, int last);
// New page/retry: detach old images and allow previously failed URLs again.
void streamrowart_reset(lv_obj_t *owner);

#endif
