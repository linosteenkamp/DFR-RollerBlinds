/**
 * @file motion.c
 * @brief TMC2209 step generation on a GPTimer. One-shot alarms: each ISR
 *        invocation emits one step pulse, schedules the next by the ramp
 *        interval, and stops at plan end (or early at the decel-stop target).
 *        ISR + data in IRAM so OTA/NVS flash writes never stall stepping.
 *
 *        Stop semantics: motion_stop() converts the remaining plan into
 *        "decelerate from here": the ISR switches its index to the mirrored
 *        decel zone of the CURRENT speed and halts after those steps.
 */
#include "motion.h"
#include "ramp.h"
#include "app_event.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"     /* esp_rom_delay_us (IRAM-safe) */
#include "esp_log.h"

static const char *TAG = "MOTION";

#define STEP_PULSE_US 3   /* TMC2209 needs only ~100 ns STEP high (DRV8825 wanted
                           * 1.9 µs); 3 µs is kept as generous headroom and is
                           * still well inside the 100 µs CRUISE_US budget. */

static motion_pins_t   s_pins;
static QueueHandle_t   s_queue;
static gptimer_handle_t s_timer;
static bool            s_reversed;

/* ---- ISR-shared state (IRAM/DRAM) ---- */
static DRAM_ATTR ramp_plan_t      s_plan;
static DRAM_ATTR volatile int32_t s_step_idx;     /* steps emitted this move */
static DRAM_ATTR volatile int32_t s_pos;          /* absolute position */
static DRAM_ATTR volatile int8_t  s_dir;          /* +1 toward Closed, -1 toward Open */
static DRAM_ATTR volatile bool    s_moving;
static DRAM_ATTR volatile bool    s_stop_req;     /* decel-stop latch */
static DRAM_ATTR volatile int32_t s_stop_at_idx;  /* index to halt at after stop req */

/* interval_at() duplicated from ramp.c's math in IRAM-safe form: the ISR must
 * not call flash-resident code. ramp_interval_us is small; we inline the same
 * computation here against s_plan. */
static IRAM_ATTR uint32_t isr_interval(int32_t idx)
{
    int32_t from_end = s_plan.total - 1 - idx;
    if (s_stop_req) {
        /* decel: mirror of how far INTO the ramp we currently are */
        int32_t remaining = s_stop_at_idx - idx;
        from_end = remaining < 0 ? 0 : remaining;
    }
    int32_t i = (idx < from_end) ? idx : from_end;
    if (i >= s_plan.accel_steps) return s_plan.cruise_us;
    uint32_t f0 = 1000000u / s_plan.start_us;
    uint32_t fc = 1000000u / s_plan.cruise_us;
    uint32_t f  = f0 + (uint32_t)(((uint64_t)(fc - f0) * (uint32_t)i)
                                  / (uint32_t)s_plan.accel_steps);
    return 1000000u / f;
}

static IRAM_ATTR bool timer_cb(gptimer_handle_t timer,
                               const gptimer_alarm_event_data_t *edata, void *ctx)
{
    (void)edata; (void)ctx;
    BaseType_t hpw = pdFALSE;

    /* emit one step pulse */
    gpio_set_level(s_pins.gpio_step, 1);
    esp_rom_delay_us(STEP_PULSE_US);
    gpio_set_level(s_pins.gpio_step, 0);
    s_pos += s_dir;
    s_step_idx++;

    bool done = s_stop_req ? (s_step_idx >= s_stop_at_idx)
                           : (s_step_idx >= s_plan.total);
    if (done) {
        gptimer_stop(timer);
        gpio_set_level(s_pins.gpio_en, 1);   /* disable driver at idle */
        s_moving = false;
        app_event_t ev = { .type = APP_EVT_MOTION_DONE,
                           .steps = s_pos, .completed = !s_stop_req };
        xQueueSendFromISR(s_queue, &ev, &hpw);
    } else {
        gptimer_alarm_config_t al = {
            .alarm_count = isr_interval(s_step_idx),
            .flags.auto_reload_on_alarm = false,
        };
        gptimer_set_raw_count(timer, 0);
        gptimer_set_alarm_action(timer, &al);
    }
    return hpw == pdTRUE;
}

esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t q)
{
    s_pins  = *pins;
    s_queue = q;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pins->gpio_step) | (1ULL << pins->gpio_dir) |
                        (1ULL << pins->gpio_en),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(pins->gpio_step, 0);
    gpio_set_level(pins->gpio_en, 1);    /* disabled at boot */

    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,        /* 1 µs ticks */
        .flags.intr_shared = false,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &s_timer));
    gptimer_event_callbacks_t cbs = { .on_alarm = timer_cb };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    ESP_LOGI(TAG, "ready (step=%d dir=%d en=%d)",
             pins->gpio_step, pins->gpio_dir, pins->gpio_en);
    return ESP_OK;
}

void motion_set_reversed(bool reversed) { s_reversed = reversed; }

esp_err_t motion_start(int32_t from_steps, int32_t to_steps,
                       const motion_profile_t *prof, int32_t hard_cap)
{
    if (s_moving) return ESP_ERR_INVALID_STATE;

    int32_t delta = to_steps - from_steps;
    if (delta == 0) {
        app_event_t ev = { .type = APP_EVT_MOTION_DONE,
                           .steps = from_steps, .completed = true };
        xQueueSend(s_queue, &ev, 0);
        return ESP_OK;
    }
    int32_t dist = delta > 0 ? delta : -delta;
    if (dist > hard_cap) {
        ESP_LOGE(TAG, "move %ld exceeds hard cap %ld — refused",
                 (long)dist, (long)hard_cap);
        return ESP_ERR_INVALID_ARG;
    }

    ramp_plan_init(&s_plan, dist, prof->cruise_us, prof->start_us, prof->accel_steps);
    s_dir      = delta > 0 ? 1 : -1;
    s_pos      = from_steps;
    s_step_idx = 0;
    s_stop_req = false;

    /* logical +1 (toward Closed) maps to a DIR level; motor_reversed flips it */
    int level = (s_dir > 0) ? 1 : 0;
    if (s_reversed) level = !level;
    gpio_set_level(s_pins.gpio_dir, level);
    gpio_set_level(s_pins.gpio_en, 0);       /* enable driver */
    esp_rom_delay_us(5);                     /* driver enable/DIR setup time */

    s_moving = true;
    gptimer_alarm_config_t al = {
        .alarm_count = s_plan.start_us,
        .flags.auto_reload_on_alarm = false,
    };
    ESP_ERROR_CHECK(gptimer_set_raw_count(s_timer, 0));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &al));
    ESP_ERROR_CHECK(gptimer_start(s_timer));
    return ESP_OK;
}

void motion_stop(void)
{
    if (!s_moving || s_stop_req) return;
    /* Halt after decelerating from the current speed: as many steps out as we
     * are currently into the ramp (capped by what remains of the plan). */
    int32_t idx      = s_step_idx;
    int32_t from_end = s_plan.total - idx;
    int32_t into     = idx < s_plan.accel_steps ? idx : s_plan.accel_steps;
    int32_t decel    = into < from_end ? into : from_end;
    if (decel < 1) decel = 1;
    s_stop_at_idx = idx + decel;
    s_stop_req    = true;
}

bool motion_is_moving(void) { return s_moving; }

int32_t motion_current_steps(void) { return s_pos; }
