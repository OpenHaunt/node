#include "oh_clock.h"

#include <sys/time.h>

#include "esp_timer.h"

/* Anything after 2023 counts as "somebody set the clock". */
#define UNIX_PLAUSIBLE_S 1700000000LL

static int64_t unix_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

bool oh_clock_is_unix(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec > UNIX_PLAUSIBLE_S;
}

int64_t oh_clock_now_ms(void)
{
    return oh_clock_is_unix() ? unix_now_ms() : esp_timer_get_time() / 1000;
}

int64_t oh_clock_from_us(int64_t esp_timer_us)
{
    if (!oh_clock_is_unix()) {
        return esp_timer_us / 1000;
    }
    int64_t age_us = esp_timer_get_time() - esp_timer_us;
    return unix_now_ms() - age_us / 1000;
}

uint32_t oh_clock_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}
