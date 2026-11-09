#pragma once
#include "esp_err.h"
#include "nvs.h"

/* User settings live in the "storage" partition. App updates do not erase it.
 * If that partition is absent, calls use the original "nvs" partition instead. */
esp_err_t storage_init(void);
esp_err_t storage_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out);
