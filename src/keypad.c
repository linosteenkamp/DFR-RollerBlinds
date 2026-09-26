/**
 * @file keypad.c
 * @brief 5 ms polling of the membrane keys (active-low, internal pull-ups).
 *        key_filter integrator -> keypad_logic classifier -> app queue.
 *        The integrator is what makes this safe on an installed unit: the
 *        harness runs beside the stepper drive, and the previous change
 *        detector turned a single coupled glitch into a full travel.
 */
#include "keypad.h"
#include "keypad_logic.h"
#include "app_event.h"
#include "key_filter.h"
#include "trace.h"
#include "motion.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define POLL_MS        5
#define FILTER_SAMPLES 12   /* net 12-sample excess to flip: 60 ms if the level
                               is cleanly settled, longer on a rattling line */
_Static_assert(FILTER_SAMPLES > 0, "integrator needs a non-zero window");
#define HOLD_MS   400
#define LONG_MS   3000
#define RESET_MS  5000

/* ---- swallowed-press telemetry ----
 *
 * A press the integrator rejects leaves no trace anywhere else: no event
 * reaches the dispatcher, so every other trace code is silent and the keypad
 * is indistinguishable from a dead one. These record the near-misses.
 *
 * The depth threshold is what separates the two populations. An excursion
 * shallower than half the window is line noise being correctly rejected —
 * the thing the integrator was added to do — and tracing it would spend the
 * ring on a non-event. Half the window or deeper is a press that nearly made
 * it, which is the interesting case.
 *
 * Coalescing is not tidiness, it is what keeps the ring readable. A line
 * producing swallowed presses is by definition noisy, and one record per
 * excursion could wrap all 256 entries in well under a second, taking every
 * MOVE record with it — destroying the context that makes the dump worth
 * reading. One record per key per second instead, carrying the deepest
 * excursion of the window and how many there were.
 *
 * Every abandoned excursion counts, shallow ones included. An earlier version
 * ignored anything under half the window as "obviously noise" — but that is
 * the judgement the data is supposed to make, and a marginal contact that
 * barely moves the line looks exactly like noise while being a real press.
 * Coalescing already bounds the cost, so the threshold bought nothing and
 * could hide the case it was added to find. */
#define SWALLOW_WINDOW_MS 1000

/* Heartbeat. 60 s suits a session of a couple of hours: 120 records against
 * 256 slots, leaving room for events while sampling the line levels often
 * enough to correlate them with motor activity. For an unattended overnight
 * run raise this — at 60 s, eight hours of heartbeats alone wrap the ring
 * nearly twice and erase whatever the soak was left running to catch. */
#define ALIVE_PERIOD_MS   60000

static const char *TAG = "KEYPAD";

static int           s_gpio[KEY_COUNT];
static key_filter_t  s_filt[KEY_COUNT];
static kp_state_t    s_kp;
static QueueHandle_t s_queue;

static uint32_t s_swallow_t[KEY_COUNT];   /* window opened at */
static uint16_t s_swallow_n[KEY_COUNT];   /* near-misses in the window */
static uint8_t  s_swallow_d[KEY_COUNT];   /* deepest of them */

static int32_t  s_latch_a = -1, s_latch_b = -1;  /* last traced latch state */
static uint32_t s_alive_t;                /* last heartbeat */
static uint32_t s_alive_polls;            /* polls since it */

/* Packed for the dump, not for the code: see TRC_KEY_LATCH in trace.h. */
static void trace_latches(void)
{
    int32_t a = (s_kp.down[KEY_UP]   ? 1 << 0 : 0) |
                (s_kp.down[KEY_DOWN] ? 1 << 1 : 0) |
                (s_kp.down[KEY_FN]   ? 1 << 2 : 0) |
                (s_kp.holding[KEY_UP]   ? 1 << 4 : 0) |
                (s_kp.holding[KEY_DOWN] ? 1 << 5 : 0);
    /* Bits 0 and 1 held in_chord and in_reset, the two suppression latches
     * that could outlive their gesture and strand the keypad. They are gone,
     * and the bit positions stay reserved so a dump written by older firmware
     * still decodes: b=1 or b=3 in an old dump is the latch fault. */
    int32_t b = (s_kp.long_fired ? 1 << 2 : 0) |
                (s_kp.reset_fired ? 1 << 3 : 0);

    /* Only transitions: the flags are steady for whole seconds at a time, and
     * a record per poll would bury everything else 200 times a second. */
    if (a != s_latch_a || b != s_latch_b) {
        s_latch_a = a;
        s_latch_b = b;
        TRACE(TRC_KEY_LATCH, a, b);
    }
}

static void post_kp(kp_event_t e)
{
    if (e.type == KP_EVT_NONE) return;
    app_event_t ev = { .type = APP_EVT_KEYPAD, .kp = e };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        TRACE(TRC_QUEUE_FULL, e.type, 0);
        ESP_LOGE(TAG, "queue full, dropped kp event type=%d", e.type);
        if (e.type == KP_EVT_HOLD_END || e.type == KP_EVT_TAP) {
            /* a dropped stop-class event must not leave the motor running */
            motion_stop();
        }
    }
}

static void poll_cb(void *arg)
{
    (void)arg;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    for (int k = 0; k < KEY_COUNT; k++) {
        if (key_filter_sample(&s_filt[k], gpio_get_level(s_gpio[k]))) {
            /* active-low: 0 = pressed. Read the debounced output rather than
             * the raw sample — they agree today, but the filter owns the
             * truth and a future change to the rail logic should not have to
             * know that the caller was relying on them matching. */
            bool pressed = key_filter_level(&s_filt[k]) == 0;

            /* A ragged edge is slow, and the classifier reads TAP vs HOLD off
             * the interval between two of them — so an edge slower than its
             * partner moves that decision without anything downstream being
             * able to tell. Silent on a clean line: only the late ones cost a
             * record. */
            uint16_t edge = key_filter_traverse(&s_filt[k]);
            if (edge > FILTER_SAMPLES) {
                TRACE(TRC_KEY_EDGE, k | (pressed ? 0x100 : 0), edge);
            }

            post_kp(kp_on_change(&s_kp, (key_id_t)k, pressed, now));
        }

        uint8_t depth = key_filter_take_swallowed(&s_filt[k]);
        if (depth > 0) {
            if (s_swallow_n[k] < UINT16_MAX) s_swallow_n[k]++;
            if (depth > s_swallow_d[k])      s_swallow_d[k] = depth;
        }
        if (s_swallow_n[k] &&
            (uint32_t)(now - s_swallow_t[k]) >= SWALLOW_WINDOW_MS) {
            TRACE(TRC_KEY_SWALLOWED, k,
                  s_swallow_d[k] | ((int32_t)s_swallow_n[k] << 16));
            s_swallow_t[k] = now;
            s_swallow_n[k] = 0;
            s_swallow_d[k] = 0;
        }
    }
    post_kp(kp_on_tick(&s_kp, now));

    /* After both classifier entry points, so a latch that a tick set or a
     * change cleared is recorded in the same poll it moved. */
    trace_latches();

    s_alive_polls++;
    if ((uint32_t)(now - s_alive_t) >= ALIVE_PERIOD_MS) {
        int32_t levels = 0;
        for (int k = 0; k < KEY_COUNT; k++) {
            levels |= (gpio_get_level(s_gpio[k])     ? 1 : 0) << k;
            levels |= (key_filter_level(&s_filt[k])  ? 1 : 0) << (k + 4);
        }
        TRACE(TRC_KEY_ALIVE, (int32_t)s_alive_polls, levels);
        s_alive_t     = now;
        s_alive_polls = 0;
    }
}

esp_err_t keypad_init(int gpio_up, int gpio_down, int gpio_fn, QueueHandle_t q)
{
    s_gpio[KEY_UP] = gpio_up;
    s_gpio[KEY_DOWN] = gpio_down;
    s_gpio[KEY_FN] = gpio_fn;
    s_queue = q;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << gpio_up) | (1ULL << gpio_down) | (1ULL << gpio_fn),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;

    /* No GPIO glitch filter on the key pins. The C6's flex glitch filter was
     * enabled here until 2026-09-26, when a bench A/B on a fresh board proved
     * it latching a released key "pressed" while the motor ran: the pin
     * measured 3.3 V, the firmware read it low for 18 s, and the jog never
     * stopped. With only the filter removed, 30 of 30 jogs stopped on
     * release. The RC front end and the key_filter integrator reject noise. */

    for (int k = 0; k < KEY_COUNT; k++) {
        key_filter_init(&s_filt[k], FILTER_SAMPLES, gpio_get_level(s_gpio[k]));
    }
    kp_init(&s_kp, HOLD_MS, LONG_MS, RESET_MS);

    static esp_timer_handle_t timer;
    const esp_timer_create_args_t targs = { .callback = poll_cb, .name = "keypad" };
    err = esp_timer_create(&targs, &timer);
    if (err != ESP_OK) return err;
    return esp_timer_start_periodic(timer, POLL_MS * 1000);
}
