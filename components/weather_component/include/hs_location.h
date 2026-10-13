#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    int32_t latitude_e6, longitude_e6;
    char city[48], timezone[64];
} hs_location_t;
/* Parse a bounded ipwho.is response. English city labels fit the subset font. */
bool hs_location_parse(const char *body, size_t size, hs_location_t *out);
