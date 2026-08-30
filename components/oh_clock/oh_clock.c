#include "oh_clock.h"

#include <sys/time.h>

#include "esp_timer.h"
#include "oh_clock_sync.h"

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

/* ── The console's clock ─────────────────────────────────────────────────── */

static oh_clock_sync_t console;
static bool console_ready;

void oh_clock_console_feed(int64_t console_ms, uint64_t seq, bool retained)
{
    if (!console_ready) {
        oh_clock_sync_init(&console);
        console_ready = true;
    }
    oh_clock_sync_feed(&console, console_ms, seq, oh_clock_now_ms(), retained);
}

int64_t oh_clock_console_now_ms(void)
{
    /* Before any sample, this node's own clock. It is the closest thing to the
     * truth there is, and rendering against it beats not rendering. */
    if (!console_ready || !oh_clock_sync_locked(&console)) {
        return oh_clock_now_ms();
    }
    return oh_clock_sync_now(&console, oh_clock_now_ms());
}

int64_t oh_clock_console_offset_ms(void)
{
    return (console_ready && oh_clock_sync_locked(&console)) ? oh_clock_sync_now(&console, 0) : 0;
}

bool oh_clock_console_locked(void)
{
    return console_ready && oh_clock_sync_locked(&console);
}
