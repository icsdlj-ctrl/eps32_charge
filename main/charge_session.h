#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CHARGE_CURRENT_THRESHOLD_A 0.05f
typedef struct {
    double charge_mah, energy_wh;
    float previous_current, previous_power;
    int64_t previous_us, start_us, low_since_us;
    bool active, previous_valid, low_pending;
    uint32_t seconds;
} charge_session_t;
/* Integration only joins consecutive valid samples, separated by <=2 seconds.
 * Invalid samples neither add charge nor count as low-current observations. */
void charge_session_update(charge_session_t *s, bool valid, float current_a, float power_w,
                           int64_t now_us, uint16_t reset_delay_seconds);
