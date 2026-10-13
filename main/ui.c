/* Native-size fonts with 4-bit coverage and lightweight vector weather icons. */
#include "station.h"
#include "font.h"
#include "hs_weather.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#ifdef ESP_PLATFORM
#include "jpeg_decoder.h"
#endif
static uint16_t *frame;
static int clip_right = LCD_WIDTH;
static const unsigned WHITE = 0xeaf2f8, MUTED = 0x94aabd, DIM = 0x637e91;
static uint16_t color(unsigned c)
{
    uint16_t v = ((c >> 8) & 0xf800) | ((c >> 5) & 0x7e0) | ((c >> 3) & 31);
    return (v << 8) | (v >> 8);
}
static void pixel(int x, int y, unsigned c)
{
    if (x >= 0 && x < clip_right && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT)
        frame[y * LCD_WIDTH + x] = color(c);
}
static void alpha(int x, int y, unsigned c, int a)
{
    if (a <= 0 || x < 0 || x >= clip_right || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
        return;
    if (a >= 15) {
        pixel(x, y, c);
        return;
    }
    uint16_t old = frame[y * LCD_WIDTH + x];
    old = (old << 8) | (old >> 8);
    unsigned r = (old >> 11) & 31, g = (old >> 5) & 63, b = old & 31;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    r = (r * (15 - a) + ((c >> 16) & 255) * a + 7) / 15;
    g = (g * (15 - a) + ((c >> 8) & 255) * a + 7) / 15;
    b = (b * (15 - a) + (c & 255) * a + 7) / 15;
    pixel(x, y, (r << 16) | (g << 8) | b);
}
static void rectangle(int x, int y, int w, int h, unsigned c)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            pixel(xx, yy, c);
}
static void gradient(unsigned c)
{
    for (int y = 0; y < LCD_HEIGHT; y++) {
        unsigned r = ((c >> 16) & 255) * (130 - y / 2) / 100,
                 g = ((c >> 8) & 255) * (130 - y / 2) / 100, b = (c & 255) * (130 - y / 2) / 100;
        rectangle(0, y, LCD_WIDTH, 1, (r << 16) | (g << 8) | b);
    }
}
static void rounded(int x, int y, int w, int h, float radius, unsigned c, int opacity)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            float dx = fmaxf(fmaxf(x + radius - .5f - xx, xx - (x + w - radius - .5f)), 0);
            float dy = fmaxf(fmaxf(y + radius - .5f - yy, yy - (y + h - radius - .5f)), 0);
            int a =
                (int)(fminf(1, fmaxf(0, radius + .5f - sqrtf(dx * dx + dy * dy))) * opacity + .5f);
            alpha(xx, yy, c, a);
        }
}
static void circle(float cx, float cy, float radius, unsigned c)
{
    for (int y = (int)floorf(cy - radius - 1); y <= ceilf(cy + radius + 1); y++)
        for (int x = (int)floorf(cx - radius - 1); x <= ceilf(cx + radius + 1); x++) {
            float dx = x - cx, dy = y - cy;
            alpha(x, y, c,
                  (int)(fminf(1, fmaxf(0, radius + .5f - sqrtf(dx * dx + dy * dy))) * 15 + .5f));
        }
}
static void line(float ax, float ay, float bx, float by, float width, unsigned c)
{
    float vx = bx - ax, vy = by - ay, length = vx * vx + vy * vy;
    for (int y = (int)floorf(fminf(ay, by) - width); y <= ceilf(fmaxf(ay, by) + width); y++)
        for (int x = (int)floorf(fminf(ax, bx) - width); x <= ceilf(fmaxf(ax, bx) + width); x++) {
            float t = length > 0 ? fminf(1, fmaxf(0, ((x - ax) * vx + (y - ay) * vy) / length)) : 0;
            float dx = x - (ax + t * vx), dy = y - (ay + t * vy);
            alpha(
                x, y, c,
                (int)(fminf(1, fmaxf(0, width * .5f + .5f - sqrtf(dx * dx + dy * dy))) * 15 + .5f));
        }
}
static unsigned next_utf8(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = *p++;
    if ((c & 0xe0) == 0xc0 && (*p & 0xc0) == 0x80)
        c = ((c & 31) << 6) | (*p++ & 63);
    else if ((c & 0xf0) == 0xe0 && p[0] && p[1] && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80) {
        c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63);
        p += 2;
    }
    *s = (const char *)p;
    return c;
}
static int measure(const char *s, station_font_t f)
{
    int w = 0;
    while (*s) {
        const station_glyph_t *g = station_glyph(next_utf8(&s), f);
        if (g)
            w += g->advance;
    }
    return w;
}
static int text(int x, int y, const char *s, station_font_t f, unsigned c)
{
    while (*s) {
        const station_glyph_t *g = station_glyph(next_utf8(&s), f);
        if (!g)
            g = station_glyph('?', f);
        if (!g)
            continue;
        if (x + g->advance > clip_right)
            break;
        for (int row = 0; row < g->height; row++)
            for (int col = 0; col < g->width; col++)
                alpha(x + g->left + col, y + g->top + row, c, station_glyph_alpha(g, col, row));
        x += g->advance;
    }
    return x;
}
static void centered(int cx, int y, const char *s, station_font_t f, unsigned c)
{
    text(cx - measure(s, f) / 2, y, s, f, c);
}
static const char *condition(int c)
{
    if (c == 0)
        return "晴";
    if (c == 1)
        return "晴间多云";
    if (c == 2)
        return "多云";
    if (c == 3)
        return "阴";
    if (c == 45 || c == 48)
        return "雾";
    if (c >= 51 && c <= 55)
        return "毛毛雨";
    if (c == 56 || c == 57 || c == 66 || c == 67)
        return "冻雨";
    if (c >= 61 && c <= 65)
        return "雨";
    if (c >= 71 && c <= 77)
        return "雪";
    if (c >= 80 && c <= 82)
        return "阵雨";
    if (c == 85 || c == 86)
        return "阵雪";
    if (c >= 95 && c <= 99)
        return "雷雨";
    return "未知";
}
static void weather_icon(int x, int y, int code, float size)
{
    float k = size / 22;
    if (code == 0 || code == 1 || code == 2) {
        float cx = x + 10 * k, cy = y + 10 * k;
        circle(cx, cy, 4.3f * k, 0xf5c86b);
        for (int i = 0; i < 8; i++) {
            float a = i * .78539816f;
            line(cx + cosf(a) * 7 * k, cy + sinf(a) * 7 * k, cx + cosf(a) * 9 * k,
                 cy + sinf(a) * 9 * k, 1.15f * k, 0xf5c86b);
        }
        if (code == 0)
            return;
    }
    unsigned cloud = code == 3 ? 0x829aab : 0xc5d3df;
    circle(x + 8 * k, y + 12 * k, 4 * k, cloud);
    circle(x + 13 * k, y + 10 * k, 5 * k, cloud);
    circle(x + 18 * k, y + 13 * k, 3 * k, cloud);
    rounded(x + 5 * k, y + 12 * k, 16 * k, 5 * k, 2 * k, cloud, 15);
    if (code >= 51 && code != 71 && code != 73 && code != 75 && code != 77 && code != 85 &&
        code != 86)
        for (int i = 0; i < 3; i++)
            line(x + (8 + i * 5) * k, y + 19 * k, x + (7 + i * 5) * k, y + 22 * k, 1.1f * k,
                 0x5fbee4);
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        for (int i = 0; i < 3; i++)
            circle(x + (8 + i * 5) * k, y + 20 * k, 1 * k, WHITE);
    if (code == 45 || code == 48) {
        line(x + 4 * k, y + 20 * k, x + 19 * k, y + 20 * k, 1 * k, MUTED);
    }
}
bool station_rotate_portrait(uint16_t *buffer)
{
    const size_t pixels = LCD_WIDTH * LCD_HEIGHT;
    uint8_t *visited = calloc((pixels + 7) / 8, 1);
    if (!visited)
        return false;
    for (size_t i = 0; i < pixels; i++) {
        if (visited[i / 8] & (1u << (i % 8)))
            continue;
        size_t current = i;
        uint16_t carried = buffer[i];
        do {
            size_t x = current % LCD_HEIGHT, y = current / LCD_HEIGHT,
                   next = (LCD_HEIGHT - 1 - x) * LCD_WIDTH + y;
            uint16_t displaced = buffer[next];
            buffer[next] = carried;
            carried = displaced;
            visited[current / 8] |= 1u << (current % 8);
            current = next;
        } while (current != i);
    }
    free(visited);
    return true;
}
extern bool station_jpeg_dimensions(const uint8_t *, size_t, int *, int *);
static void background(const station_snapshot_t *s, int page)
{
    unsigned rgb = s->config.skin == 2 ? 0x17120a : s->config.skin == 1 ? 0x061a24 : 0x070c14;
    gradient(rgb);
#ifdef ESP_PLATFORM
    if (s->config.skin == 3 && page != PAGE_WEATHER) {
        uint8_t *jpeg;
        size_t n;
        int w, h;
        if (station_image_read(page == PAGE_BOOT ? 0 : page == PAGE_CLOCK ? 1 : 2, &jpeg, &n)) {
            if (station_jpeg_dimensions(jpeg, n, &w, &h)) {
                esp_jpeg_image_cfg_t cfg = {.indata = jpeg,
                                            .indata_size = n,
                                            .outbuf = (uint8_t *)frame,
                                            .outbuf_size = LCD_WIDTH * LCD_HEIGHT * 2,
                                            .out_format = JPEG_IMAGE_FORMAT_RGB565,
                                            .out_scale = JPEG_IMAGE_SCALE_0,
                                            .flags.swap_color_bytes = 1};
                esp_jpeg_image_output_t image;
                esp_err_t decoded = esp_jpeg_decode(&cfg, &image);
                if (decoded == ESP_OK && w == LCD_HEIGHT && h == LCD_WIDTH)
                    station_rotate_portrait(frame);
            }
            free(jpeg);
        }
    }
#endif
}

static void draw_charge(const station_snapshot_t *s, unsigned accent)
{
    const char *names[] = {"A1", "C1", "C2", "C3"};
    char b[48];
    float total = 0;
    for (int i = 0; i < 4; i++) {
        int x = i * 71;
        clip_right = x + 69;
        const station_port_t *p = s->ports + i;
        rounded(x + 2, 2, 67, 72, 5, 0x19313f, s->config.skin == 3 ? 11 : 15);
        rounded(x + 3, 3, 65, 70, 4, 0x0b1a25, s->config.skin == 3 ? 10 : 15);
        text(x + 7, 3, names[i], FONT_SMALL, MUTED);
        if (p->valid) {
            snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(p->seconds / 3600),
                     (unsigned long)(p->seconds / 60 % 60));
            text(x + 27, 3, b, FONT_SMALL, DIM);
        }
        circle(x + 61, 9, 1.7f, p->connected ? 0x62dab3 : 0x405666);
        if (p->valid) {
            snprintf(b, sizeof(b), p->power < 100 ? "%.1f" : "%.0f", p->power);
            int end = text(x + 7, 13, b, FONT_VALUE, WHITE);
            text(end + 1, 23, "W", FONT_SMALL, MUTED);
            snprintf(b, sizeof(b), "%.1fV %.2fA", p->voltage, p->current);
            text(x + 7, 37, b, FONT_SMALL, DIM);
            if (p->charge_mah < 99999)
                snprintf(b, sizeof(b), "%.0fmAh", p->charge_mah);
            else
                snprintf(b, sizeof(b), "%.1fAh", p->charge_mah / 1000);
            text(x + 7, 49, b, FONT_SMALL, accent);
            snprintf(b, sizeof(b), "%.2fWh", p->energy_wh);
            text(x + 7, 61, b, FONT_SMALL, MUTED);
            total += p->power;
        } else {
            text(x + 7, 14, "--", FONT_VALUE, DIM);
            text(x + 7, 39, "未检测到", FONT_SMALL, DIM);
            text(x + 7, 61, "--mAh", FONT_SMALL, DIM);
        }
    }
    clip_right = LCD_WIDTH;
    int width = (int)(LCD_WIDTH * fmaxf(0, fminf(1, total / fmaxf(1, s->config.max_power))));
    rectangle(0, 75, width, 1, accent);
}
static void city_text(int x, int y, int width, const hs_weather_snapshot_t *w)
{
    const char *city = w->config.city[0] ? w->config.city : "IP定位";
    station_font_t font = measure(city, FONT_BODY) <= width ? FONT_BODY : FONT_SMALL;
    int saved = clip_right;
    clip_right = x + width;
    if (measure(city, font) <= width)
        text(x, y, city, font, MUTED);
    else {
        char short_name[56] = {0};
        const char *end = city;
        int used = 0, limit = width - measure("...", font);
        while (*end) {
            const char *next = end;
            const station_glyph_t *g = station_glyph(next_utf8(&next), font);
            if (!g || used + g->advance > limit)
                break;
            used += g->advance;
            end = next;
        }
        size_t n = end - city;
        memcpy(short_name, city, n);
        strcpy(short_name + n, "...");
        text(x, y, short_name, font, MUTED);
    }
    clip_right = saved;
}
static void draw_clock(const station_snapshot_t *s, const struct tm *local, bool valid,
                       unsigned accent)
{
    char b[80];
    text(8, 3, "HOLO STATION", FONT_SMALL, DIM);
    circle(145, 8, 1.6f, s->wifi_connected ? 0x62dab3 : 0x536c7f);
    if (valid)
        strftime(b, sizeof(b), "%H:%M", local);
    else
        strcpy(b, "--:--");
    text(5, 14, b, FONT_CLOCK, WHITE);
    if (valid) {
        snprintf(b, sizeof(b), "%02d", local->tm_sec);
        text(135, 48, b, FONT_SMALL, DIM);
        const char *week[] = {"日", "一", "二", "三", "四", "五", "六"};
        snprintf(b, sizeof(b), "%04d.%02d.%02d  周%s", local->tm_year + 1900, local->tm_mon + 1,
                 local->tm_mday, week[local->tm_wday]);
    } else
        strcpy(b, "等待网络校时");
    text(8, 63, b, FONT_SMALL, MUTED);
    rectangle(157, 8, 1, 59, 0x213443);
    hs_weather_snapshot_t w = {0};
    hs_weather_get(&w);
    city_text(169, 3, 73, &w);
    if (w.has_data) {
        weather_icon(252, 1, w.forecast.weather_code, 20);
        if (w.stale || w.location_cached)
            circle(245, 9, 1.5f, 0xf5c86b);
        snprintf(b, sizeof(b), "%.1f", w.forecast.temperature_c);
        int end = text(166, 24, b, FONT_TEMP, WHITE);
        text(end + 3, 33, "℃", FONT_BODY, accent);
        text(169, 58, condition(w.forecast.weather_code), FONT_BODY, MUTED);
    } else {
        text(169, 31, s->wifi_connected ? "天气加载中" : "等待配网", FONT_BODY, DIM);
    }
}
static void draw_weather(const station_snapshot_t *s, unsigned accent)
{
    (void)s;
    hs_weather_snapshot_t w = {0};
    char b[72];
    bool loaded = hs_weather_get(&w) == ESP_OK && w.has_data;
    city_text(7, 2, 109, &w);
    rectangle(123, 6, 1, 61, 0x213443);
    if (loaded) {
        snprintf(b, sizeof(b), "%.1f", w.forecast.temperature_c);
        int end = text(5, 18, b, FONT_TEMP, WHITE);
        text(end + 3, 27, "℃", FONT_BODY, accent);
        text(7, 47, condition(w.forecast.weather_code), FONT_BODY, MUTED);
        if (w.stale)
            text(91, 48, "缓存", FONT_SMALL, 0xf5c86b);
        if (w.forecast.humidity >= 0)
            snprintf(b, sizeof(b), "RH %d%%  %.0fkm/h", w.forecast.humidity, w.forecast.wind_kmh);
        else
            snprintf(b, sizeof(b), "RH --  %.0fkm/h", w.forecast.wind_kmh);
        text(7, 64, b, FONT_SMALL, DIM);
    } else {
        text(7, 20, "--", FONT_TEMP, DIM);
        text(7, 53,
             w.status == HS_WEATHER_WAIT_CLOCK ? "等待校时"
             : w.status == HS_WEATHER_LOCATING ? "正在定位"
             : s->wifi_connected               ? "等待天气"
                                               : "请先联网",
             FONT_BODY, DIM);
    }
    const char *days[] = {"今天", "明天", "后天"};
    for (int i = 0; i < 3; i++) {
        int center = 151 + i * 49;
        centered(center, 3, days[i], FONT_SMALL, i ? MUTED : accent);
        if (loaded) {
            const hs_weather_day_t *d = &w.forecast.days[i];
            weather_icon(center - 10, 18, d->weather_code, 18);
            snprintf(b, sizeof(b), "%.0f/%.0f℃", d->minimum_c, d->maximum_c);
            centered(center, 42, b, FONT_SMALL, WHITE);
            if (d->rain_probability >= 0)
                snprintf(b, sizeof(b), "%d%%", d->rain_probability);
            else
                strcpy(b, "--");
            centered(center, 54, b, FONT_SMALL, DIM);
        } else
            centered(center, 36, "--", FONT_SMALL, DIM);
    }
    text(133, 66, "Open-Meteo", FONT_SMALL, DIM);
    if (loaded)
        text(239, 66, w.forecast.observed_at + 11, FONT_SMALL, DIM);
}
void station_render(uint16_t *buffer, const station_snapshot_t *s, int page, const struct tm *local,
                    bool valid)
{
    frame = buffer;
    clip_right = LCD_WIDTH;
    unsigned accent = s->config.skin == 2 ? 0xf5c86b : 0x68d8ce;
    background(s, page);
    if (s->ota_busy) {
        text(10, 7, "固件升级中", FONT_BODY, accent);
        char b[20];
        snprintf(b, sizeof(b), "%u", s->ota_progress);
        int end = text(10, 28, b, FONT_TEMP, WHITE);
        text(end + 3, 37, "%", FONT_BODY, MUTED);
        rounded(104, 35, 166, 5, 2, 0x213443, 15);
        rounded(104, 35, s->ota_progress * 166 / 100, 5, 2, accent, 15);
    } else if (page == PAGE_BOOT) {
        centered(142, 15, "HOLO STATION", FONT_BODY, accent);
        centered(142, 46, "IP天气 / 充电监测", FONT_SMALL, MUTED);
    } else if (page == PAGE_CHARGE)
        draw_charge(s, accent);
    else if (page == PAGE_WEATHER)
        draw_weather(s, accent);
    else
        draw_clock(s, local, valid, accent);
}
