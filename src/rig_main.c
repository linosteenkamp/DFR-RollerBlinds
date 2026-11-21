/**
 * DFR gearbox test rig — wiring. All the decisions are in rig_ctl.c. This
 * file has the GPIO map and the constants, the module init, the ports
 * table that connects rig_ctl to the real modules, and the dispatcher loop
 * that gives rig_ctl one event at a time.
 *
 * The rig firmware has no Zigbee and no keypad. Refer to RIG_GUIDE.md and
 * to the spec docs/superpowers/specs/2026-11-21-gearbox-test-rig-design.md.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "motion.h"
#include "ramp.h"
#include "rf_switch.h"
#include "rig_console.h"
#include "rig_ctl.h"
#include "rig_event.h"

static const char *TAG = "RIG";

/* ---- GPIO map: XIAO ESP32C6, D-number → GPIO (refer to the spec) ----
 * The driver pins are the same as on the blind. */
#define PIN_STEP     19     /* D8 */
#define PIN_DIR      17     /* D7 */
#define PIN_EN       20     /* D9 */
#define PIN_RF_SW_EN 3      /* low = switch enabled */
#define PIN_RF_ANT   14     /* low = ceramic, high = U.FL */

/* ---- motion: the same acceleration as the blind (spec, story 40) ---- */
#define START_US     500
#define ACCEL_STEPS  800
/* The travel is about 13.6 output revolutions (326 400 microsteps). The
 * cap is a safety limit for one move, a little more than the full travel. */
#define HARD_CAP     400000

/* The speed sweep needs 40 µs (spec, story 38). The rig env sets the floor
 * with a build flag. This check stops a build that did not get the flag. */
_Static_assert(RAMP_MIN_CRUISE_US == 40u, "the rig env must set RAMP_MIN_CRUISE_US=40u");

#define CMD_QUEUE_LEN 8

static QueueHandle_t    s_queue;    /* commands */
static QueueHandle_t    s_done_q;   /* motion_done_t only, one slot */
static QueueSetHandle_t s_qset;     /* s_queue + s_done_q */
static rig_t            s_rig;      /* owned by the dispatcher task */

static const rig_ports_t PORTS = {
    .motion_start = motion_start,
    .motion_stop  = motion_stop,
    .motion_steps = motion_current_steps,
    .publish      = rig_console_publish,
};

static const rig_config_t CFG = {
    .start_us        = START_US,
    .accel_steps     = ACCEL_STEPS,
    .min_interval_us = RAMP_MIN_CRUISE_US,   /* the rig env sets 40 µs */
    .max_interval_us = RAMP_MAX_CRUISE_US,
    .hard_cap        = HARD_CAP,
};

static void dispatcher_task(void *pv)
{
    (void)pv;
    rig_event_t ev;
    uint32_t done_fail_seen = 0;
    for (;;) {
        QueueSetMemberHandle_t q = xQueueSelectFromSet(s_qset, portMAX_DELAY);
        if (q == NULL) continue;
        if (q == s_done_q) {
            /* motion posts its own type; convert it to the rig event. */
            motion_done_t d;
            if (xQueueReceive(s_done_q, &d, 0) != pdTRUE) continue;
            ev = (rig_event_t){ .type = RIG_EVT_MOTION_DONE,
                                .done = { .steps = d.steps, .completed = d.completed } };
        } else if (xQueueReceive(q, &ev, 0) != pdTRUE) {
            continue;
        }
        rig_handle(&s_rig, &ev);
        uint32_t f = motion_done_post_failures();
        if (f != done_fail_seen) {
            done_fail_seen = f;
            ESP_LOGE(TAG, "motion_done post failed (%lu)", (unsigned long)f);
        }
    }
}

void app_main(void)
{
    /* The radio is not used yet. Ticket #36 adds Wi-Fi. The switch is
     * set now, so that it is correct before the radio starts. */
    ESP_ERROR_CHECK(rf_switch_init(PIN_RF_SW_EN, PIN_RF_ANT, RF_ANT_UFL));

    s_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(rig_event_t));
    configASSERT(s_queue);
    s_done_q = xQueueCreate(1, sizeof(motion_done_t));
    configASSERT(s_done_q);
    s_qset = xQueueCreateSet(CMD_QUEUE_LEN + 1);        /* the sum of the member lengths */
    configASSERT(s_qset);
    xQueueAddToSet(s_queue, s_qset);        /* the two are empty here, as required */
    xQueueAddToSet(s_done_q, s_qset);

    rig_init(&s_rig, &PORTS, &CFG);

    motion_pins_t pins = { .gpio_step = PIN_STEP, .gpio_dir = PIN_DIR, .gpio_en = PIN_EN };
    ESP_ERROR_CHECK(motion_init(&pins, s_done_q));
    /* Hold at idle stays off (the driver is disabled after each move).
     * Ticket #33 sets IHOLD through UART, and then sets hold at idle. */
    motion_set_hold_at_idle(false);

    ESP_ERROR_CHECK(rig_console_init(s_queue));

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "test rig ready. Commands: move <steps> <interval_us>, stop, status "
                  "(interval_us %u..%u)", (unsigned)RAMP_MIN_CRUISE_US,
             (unsigned)RAMP_MAX_CRUISE_US);
}
