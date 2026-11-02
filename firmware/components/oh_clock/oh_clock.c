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

/*
 * Estimated against esp_timer, not against oh_clock_now_ms(). The wall clock steps
 * when SNTP first sets it, by the whole of unix time if it was counting from boot,
 * and an offset measured before the step is wrong by that much after it. On wifi
 * a node is on the broker before SNTP answers, so it locked first, stepped, and
 * then slewed out of the error at 50 ms a second: show time decades ahead, and no
 * stream or clip ever played. Measured against a clock that never steps, the
 * offset stays right whatever SNTP does.
 */
static oh_clock_sync_t console;
static bool console_ready;

static int64_t mono_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void oh_clock_console_feed(int64_t console_ms, uint64_t seq, bool retained)
{
    if (!console_ready) {
        oh_clock_sync_init(&console);
        console_ready = true;
    }
    oh_clock_sync_feed(&console, console_ms, seq, mono_ms(), retained);
}

bool oh_clock_console_locked(void)
{
    return console_ready && oh_clock_sync_locked(&console);
}

int64_t oh_clock_console_now_ms(void)
{
    /* Before any sample, this node's own clock. It is the closest thing to the
     * truth there is, and rendering against it beats not rendering. */
    if (!oh_clock_console_locked()) {
        return oh_clock_now_ms();
    }
    return oh_clock_sync_now(&console, mono_ms());
}

/* Relative to this node's own clock, as /status and the console show it. */
int64_t oh_clock_console_offset_ms(void)
{
    return oh_clock_console_locked() ? oh_clock_console_now_ms() - oh_clock_now_ms() : 0;
}

int64_t oh_clock_console_now_us(void)
{
    if (!oh_clock_console_locked()) {
        if (!oh_clock_is_unix()) {
            return esp_timer_get_time();
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    }
    return esp_timer_get_time() + oh_clock_sync_now(&console, 0) * 1000;
}
