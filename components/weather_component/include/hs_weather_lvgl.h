#ifndef HS_WEATHER_LVGL_H
#define HS_WEATHER_LVGL_H
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    lv_obj_t *root;
    lv_obj_t *city;
    lv_obj_t *current;
    lv_obj_t *details;
    lv_obj_t *environment;
    lv_obj_t *days[3];
    lv_obj_t *status;
    lv_obj_t *credit;
} hs_weather_view_t;

/* Both calls must run from the existing LVGL UI task/under its mutex.
 * The worker never calls LVGL. Requires LVGL 8/9 with flex enabled.
 * Parent should be a dedicated page, with an appropriate small font. */
void hs_weather_lvgl_create(lv_obj_t *parent, hs_weather_view_t *view);
void hs_weather_lvgl_update(hs_weather_view_t *view);
#ifdef __cplusplus
}
#endif
#endif
