#include "oh_amp.h"

void oh_amp_init(oh_amp_t *a, int64_t idle_us, bool always_on)
{
    a->on = false;
    a->quiet_since_us = 0;
    a->idle_us = idle_us;
    a->always_on = always_on;
}

bool oh_amp_step(oh_amp_t *a, bool wanted, int64_t now_us)
{
    if (wanted || a->always_on) {
        a->quiet_since_us = 0;
        if (a->on) {
            return false;
        }
        a->on = true;
        return true;
    }
    if (!a->on) {
        return false;
    }
    if (!a->quiet_since_us) {
        a->quiet_since_us = now_us;
        return false;
    }
    if (now_us - a->quiet_since_us > a->idle_us) {
        a->on = false;
        return true;
    }
    return false;
}
