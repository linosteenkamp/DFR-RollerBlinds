/**
 * @file trace.c
 * @brief RTC-backed decision log. Survives every reset — watchdog, panic,
 *        brownout, and the DTR/RTS reset that attaching a serial cable
 *        causes — but not power removal. That is the point: the reset used
 *        to destroy the evidence, and now it prints it.
 */
#include "trace.h"

#include <stdio.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#if TRACE_ENABLED

static const char *TAG = "TRACE";

/* XORed with the ring's own size so a layout change (a field added to
 * trace_rec_t, trace_code_t renumbered) changes the magic too. Without this,
 * a reflash — esptool resets the chip, it does not cut power — leaves the
 * previous build's ring in place with a magic that still matches, and the
 * first dump prints stale data as current. */
#define TRACE_MAGIC (0x54524331u ^ (uint32_t)sizeof(trace_ring_t))

RTC_NOINIT_ATTR static trace_ring_t s_ring;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static const char *code_name(uint16_t c)
{
    switch (c) {
    case TRC_BOOT:         return "BOOT";
    case TRC_KEY_EVENT:    return "KEY_EVENT";
    case TRC_MOVE_START:   return "MOVE_START";
    case TRC_MOVE_NOOP:    return "MOVE_NOOP";
    case TRC_MOVE_REFUSED: return "MOVE_REFUSED";
    case TRC_MOVE_DONE:    return "MOVE_DONE";
    case TRC_ZB_CMD:       return "ZB_CMD";
    case TRC_QUEUE_FULL:   return "QUEUE_FULL";
    case TRC_KEY_SWALLOWED: return "KEY_SWALLOWED";
    case TRC_KEY_EDGE:      return "KEY_EDGE";
    case TRC_KEY_LATCH:     return "KEY_LATCH";
    case TRC_KEY_ALIVE:     return "KEY_ALIVE";
    case TRC_DEADMAN_STOP:     return "DEADMAN_STOP";
    case TRC_DONE_POST_FAILED: return "DONE_POST_FAIL";
    default: {
        /* Keep the number: an unrecognised code is the one clue that this
         * dump is a stale-layout ring (see TRACE_MAGIC above) rather than a
         * bug in this table. Single-threaded, boot-time-only caller, so a
         * reused static buffer is safe. */
        static char buf[16];
        snprintf(buf, sizeof buf, "?%u", (unsigned)c);
        return buf;
    }
    }
}

void trace_init(void)
{
    if (!trace_ring_valid(&s_ring, TRACE_MAGIC)) {
        trace_ring_reset(&s_ring, TRACE_MAGIC);
    }
}

void trace_emit(uint16_t code, int32_t a, int32_t b)
{
    uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    portENTER_CRITICAL_SAFE(&s_mux);
    trace_ring_push(&s_ring, t, code, a, b);
    portEXIT_CRITICAL_SAFE(&s_mux);
}

void trace_dump(void)
{
    uint32_t n = trace_ring_count(&s_ring);
    int reason = (int)esp_reset_reason();
    if (n == 0) {
        ESP_LOGI(TAG, "no records (reset reason %d)", reason);
        return;
    }
    ESP_LOGI(TAG, "%u records, seq %u..%u, reset reason %d", (unsigned)n,
             (unsigned)trace_ring_at(&s_ring, 0)->seq,
             (unsigned)trace_ring_at(&s_ring, n - 1)->seq,
             reason);
    for (uint32_t i = 0; i < n; i++) {
        const trace_rec_t *r = trace_ring_at(&s_ring, i);
        ESP_LOGI(TAG, "[%9u] %-12s a=%ld b=%ld",
                 (unsigned)r->t_ms, code_name(r->code),
                 (long)r->a, (long)r->b);
    }
}

#else  /* !TRACE_ENABLED — app_main calls these unconditionally, so they must link */

void trace_init(void) {}
void trace_dump(void) {}
void trace_emit(uint16_t code, int32_t a, int32_t b)
{
    (void)code; (void)a; (void)b;
}

#endif
