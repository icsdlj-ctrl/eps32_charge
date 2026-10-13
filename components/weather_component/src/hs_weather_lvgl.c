#include "hs_weather_lvgl.h"
#include "hs_weather.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_width(obj,lv_pct(100));
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);
    lv_label_set_text(obj,text);
    return obj;
}

void hs_weather_lvgl_create(lv_obj_t *parent, hs_weather_view_t *v)
{
    if (!parent || !v) return;
    memset(v,0,sizeof(*v));
    v->root = lv_obj_create(parent);
    lv_obj_set_size(v->root,lv_pct(100),lv_pct(100));
    lv_obj_set_style_pad_all(v->root,2,0);
    lv_obj_set_style_pad_row(v->root,0,0);
    lv_obj_set_style_pad_column(v->root,0,0);
    lv_obj_set_style_border_width(v->root,0,0);
    lv_obj_set_style_radius(v->root,0,0);
    lv_obj_set_style_bg_color(v->root,lv_color_hex(0x07111f),0);
    lv_obj_set_style_text_color(v->root,lv_color_hex(0xe8f2ff),0);
    lv_obj_set_scrollbar_mode(v->root,LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(v->root,LV_OBJ_FLAG_SCROLLABLE);
#if LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(v->root,&lv_font_montserrat_12,0);
#endif
    lv_obj_set_flex_flow(v->root,LV_FLEX_FLOW_ROW);
    lv_obj_t *left = lv_obj_create(v->root), *right = lv_obj_create(v->root);
    lv_obj_t *columns[] = {left,right};
    for (int i = 0; i < 2; ++i) {
        lv_obj_remove_style_all(columns[i]);
        lv_obj_set_height(columns[i],lv_pct(100));
        lv_obj_set_flex_flow(columns[i],LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(columns[i],0,0);
        lv_obj_clear_flag(columns[i],LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_set_width(left,lv_pct(36));
    lv_obj_set_width(right,lv_pct(64));
    v->city = label(left,"Weather");
    v->current = label(left,"-- C");
    v->details = label(left,"--");
    v->status = label(left,"Set location");
    v->environment = label(right,"RH -- Wind --");
    for (int i = 0; i < 3; ++i) v->days[i] = label(right,"--");
    v->credit = label(right,"Open-Meteo.com");
    hs_weather_lvgl_update(v);
}

void hs_weather_lvgl_update(hs_weather_view_t *v)
{
    if (!v || !v->root) return;
    hs_weather_snapshot_t s;
    if (hs_weather_get(&s) != ESP_OK) {
        lv_label_set_text(v->status,"Weather not started");
        return;
    }
    lv_label_set_text(v->city,s.config.city[0]?s.config.city:"Weather");
    char text[128];
    if (s.has_data) {
        snprintf(text,sizeof(text),"%.1f C",s.forecast.temperature_c);
        lv_label_set_text(v->current,text);
        lv_label_set_text(v->details,hs_weather_condition(s.forecast.weather_code));
        if (s.forecast.humidity >= 0)
            snprintf(text,sizeof(text),"RH %d%%  Wind %.0f km/h",s.forecast.humidity,s.forecast.wind_kmh);
        else snprintf(text,sizeof(text),"RH --  Wind %.0f km/h",s.forecast.wind_kmh);
        lv_label_set_text(v->environment,text);
        for (int i = 0; i < 3; ++i) {
            const hs_weather_day_t *d = &s.forecast.days[i];
            if (d->rain_probability >= 0)
                snprintf(text,sizeof(text),"%.5s %.0f/%.0f C %d%%",d->date+5,d->minimum_c,d->maximum_c,d->rain_probability);
            else snprintf(text,sizeof(text),"%.5s %.0f/%.0f C --",d->date+5,d->minimum_c,d->maximum_c);
            lv_label_set_text(v->days[i],text);
        }
        snprintf(text,sizeof(text),"%s %lum",s.stale?"Cached":"Updated",(unsigned long)(s.age_seconds/60));
        lv_label_set_text(v->status,text);
    } else {
        lv_label_set_text(v->current,"-- C");
        lv_label_set_text(v->details,"--");
        lv_label_set_text(v->environment,"RH -- Wind --");
        for (int i = 0; i < 3; ++i) lv_label_set_text(v->days[i],"--");
        const char *status = "Loading weather";
        switch (s.status) {
        case HS_WEATHER_UNCONFIGURED: status = "Set a location"; break;
        case HS_WEATHER_WAIT_NETWORK: status = "Waiting for Wi-Fi"; break;
        case HS_WEATHER_WAIT_CLOCK: status = "Waiting for clock sync"; break;
        case HS_WEATHER_ERROR: status = "Weather unavailable"; break;
        default: break;
        }
        lv_label_set_text(v->status,status);
    }
}
