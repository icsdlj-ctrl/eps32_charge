#pragma once
#include <stdint.h>
typedef enum { FONT_SMALL, FONT_BODY, FONT_VALUE, FONT_TEMP, FONT_CLOCK } station_font_t;
typedef struct { uint16_t codepoint; uint8_t width,height,advance; int8_t left,top; uint32_t offset; } station_glyph_t;
const station_glyph_t *station_glyph(unsigned codepoint, station_font_t font);
uint8_t station_glyph_alpha(const station_glyph_t *glyph, int x, int y);
