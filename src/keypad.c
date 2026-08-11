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
#include "motion.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define POLL_MS        5
#define FILTER_SAMPLES 12   /* 12 x 5 ms = 60 ms of settled level per edge */
#define HOLD_MS   400
#define LONG_MS   3000
#define RESET_MS  5000

static const char *TAG = "KEYPAD";

static int           s_gpio[KEY_COUNT];
static key_filter_t  s_filt[KEY_COUNT];
static kp_state_t    s_kp;
static QueueHandle_t s_queue;

static void post_kp(kp_event_t e)
{
    if (e.type == KP_EVT_NONE) return;
    app_event_t ev = { .type = APP_EVT_KEYPAD, .kp = e };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
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
            post_kp(kp_on_change(&s_kp, (key_id_t)k,
                                 key_filter_level(&s_filt[k]) == 0, now));
        }
    }
    post_kp(kp_on_tick(&s_kp, now));
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
