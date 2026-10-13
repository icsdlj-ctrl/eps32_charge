#include "charge_session.h"
#include <math.h>
#include <string.h>

void charge_session_update(charge_session_t *s, bool valid, float current_a, float power_w,
                           int64_t now_us, uint16_t reset_delay_seconds)
{
    if (!s)
        return;
    valid = valid && isfinite(current_a) && isfinite(power_w) && now_us >= 0;
    if (!valid) {
        s->previous_valid = false;
        s->low_pending = false;
        return;
    }
    bool load = current_a >= CHARGE_CURRENT_THRESHOLD_A;
    if (!s->active && load) {
        memset(s, 0, sizeof(*s));
        s->active = true;
        s->start_us = now_us;
    }
    if (!s->active)
        return;
    int64_t elapsed = now_us - s->previous_us;
    bool consecutive = s->previous_valid && elapsed > 0 && elapsed <= 2000000;
    if (!consecutive)
        s->low_pending = false;
    if (load)
        s->low_pending = false;
    else {
        if (!s->low_pending) {
            s->low_pending = true;
            s->low_since_us = now_us;
        }
        if (now_us - s->low_since_us >= reset_delay_seconds * 1000000LL) {
            memset(s, 0, sizeof(*s));
            return;
        }
    }
    if (consecutive) {
        double seconds = elapsed / 1000000.0;
        double amps = (fmaxf(0, s->previous_current) + fmaxf(0, current_a)) * .5;
        double watts = (s->previous_power + (current_a > 0 ? fmaxf(0, power_w) : 0)) * .5;
        s->charge_mah += amps * seconds / 3.6;
        s->energy_wh += watts * seconds / 3600.0;
    }
    s->previous_current = current_a;
    s->previous_power = current_a > 0 ? fmaxf(0, power_w) : 0;
    s->previous_us = now_us;
    s->previous_valid = true;
    s->seconds = now_us >= s->start_us ? (uint32_t)((now_us - s->start_us) / 1000000) : 0;
}
