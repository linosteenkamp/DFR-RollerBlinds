/**
 * DFR-RollerBlinds — wiring. Every decision lives in ctl.c; this file owns
 * the GPIO map and constants, module init, the ports table that binds ctl to
 * the real modules, and the dispatcher loop that feeds ctl one event at a
 * time.
 */
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "zb_core.h"
#include "fw_version.h"
#include "app_event.h"
#include "position.h"
#include "motion.h"
#include "blind_store.h"
#include "keypad.h"
#include "status_led.h"
#include "covering.h"
#include "ramp.h"
#include "trace.h"
#include "ctl.h"
#include "esp_system.h"   /* esp_reset_reason */

static const char *TAG = "BLINDS";

/* ---- identity ---- */
#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x10" "DFR-RollerBlinds"
#define APP_ENDPOINT 1

/* ---- GPIO map: XIAO ESP32C6, D-number → GPIO (see HARDWARE.md) ----
 * The D-numbers are what the silkscreen and the wiring harness use; the
 * GPIO numbers below are what the driver API wants. Grouped to match the
 * implementation board's physical layout: driver signals on D7-D9 at one
 * end of the header, keypad on D4-D6 at the other, LED on D3.
 * D6/D7 carry the C6's default UART0 pins — free here because the console
 * runs on USB-Serial-JTAG (CONFIG_ESP_CONSOLE_UART_NUM = -1). */
#define PIN_STEP     19     /* D8 */
#define PIN_DIR      17     /* D7 */
#define PIN_EN       20     /* D9 */
#define PIN_BTN_UP   22     /* D4 */
#define PIN_BTN_DOWN 23     /* D5 */
#define PIN_BTN_FN   16     /* D6 */
#define PIN_LED_EXT  21     /* D3 — sole indicator; no onboard mirror on XIAO */

/* ---- motion tuning (bench constants, spec §6) ---- */
#define START_US        500      /* ~2 kHz first/last step */
#define ACCEL_STEPS     800
#define JOG_CRUISE_US   300      /* jog slower than travel but fast enough to
                                  * cross a full 2.5 m span within the cal timeout */
#define JOG_START_US    900      /* jog starts slower than its cruise */
#define MIN_SPAN_STEPS  6000     /* ~1/4 output rev: min valid calibration */
#define HARD_CAP_MARGIN 2400     /* watchdog: allowed overshoot of span */
#define JOG_UNBOUNDED   2000000  /* "infinite" jog target while uncalibrated */
#define CAL_TIMEOUT_US  (10LL * 60 * 1000000)  /* must exceed a full-span jog */
#define REPORT_PERIOD_US (1LL * 1000000)

static QueueHandle_t      s_queue;
static ctl_t              s_ctl;          /* owned by the dispatcher task */
static esp_timer_handle_t s_cal_timer;
static esp_timer_handle_t s_report_timer;

/* ---------- ports: ctl's side effects, bound to the real modules ---------- */

static void p_cal_timer_start(void)    { esp_timer_start_once(s_cal_timer, CAL_TIMEOUT_US); }
static void p_cal_timer_stop(void)     { esp_timer_stop(s_cal_timer); }
static void p_report_timer_start(void) { esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US); }
static void p_report_timer_stop(void)  { esp_timer_stop(s_report_timer); }
static void p_trace(uint16_t code, int32_t a, int32_t b) { TRACE(code, a, b); }
static void p_log(const char *msg)     { ESP_LOGW(TAG, "%s", msg); }

static const ctl_ports_t PORTS = {
    .motion_start        = motion_start,
    .motion_stop         = motion_stop,
    .motion_is_moving    = motion_is_moving,
    .motion_steps        = motion_current_steps,
    .motion_set_reversed = motion_set_reversed,
    .save_move_flag      = blind_store_set_move_flag,
    .save_span           = blind_store_save_span,
    .save_position       = blind_store_save_position,
    .save_reversed       = blind_store_save_motor_reversed,
    .save_travel_time    = blind_store_save_travel_time,
    .set_motion_allowed  = covering_set_motion_allowed,
    .set_operational     = covering_set_operational,
    .report_lift         = covering_report_lift,
    .report_mode         = covering_report_mode,
    .report_travel_time  = covering_report_travel_time,
    .net_joined          = zb_core_is_joined,
    .factory_reset       = zb_core_factory_reset,
    .led_set             = status_led_set,
    .led_flash           = status_led_flash,
    .cal_timer_start     = p_cal_timer_start,
    .cal_timer_stop      = p_cal_timer_stop,
    .report_timer_start  = p_report_timer_start,
    .report_timer_stop   = p_report_timer_stop,
    .trace               = p_trace,
    .log                 = p_log,
};

static const ctl_config_t CFG = {
    .move            = { RAMP_DEFAULT_CRUISE_US, START_US, ACCEL_STEPS },
    .jog             = { JOG_CRUISE_US, JOG_START_US, ACCEL_STEPS },
    .min_span_steps  = MIN_SPAN_STEPS,
    .hard_cap_margin = HARD_CAP_MARGIN,
    .jog_unbounded   = JOG_UNBOUNDED,
};

/* ---------- event producers (post only; ctl decides) ---------- */

static void cal_timeout_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_CAL_TIMEOUT };
    xQueueSend(s_queue, &ev, 0);
}

static void report_tick_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_REPORT_TICK };
    xQueueSend(s_queue, &ev, 0);
}

/* Zigbee stack context (lock held): enqueue only. */
static void zb_network_lost_cb(void)
{
    app_event_t ev = { .type = APP_EVT_ZB_NET_LOST };
    xQueueSend(s_queue, &ev, 0);
}

/* Zigbee stack context (lock held): enqueue only. */
static void zb_network_joined_cb(void)
{
    app_event_t ev = { .type = APP_EVT_ZB_NET_JOINED };
    xQueueSend(s_queue, &ev, 0);
}

static void dispatcher_task(void *pv)
{
    (void)pv;
    app_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        ctl_handle(&s_ctl, &ev);
    }
}

/* ---------- boot ---------- */

void app_main(void)
{
    trace_init();                              /* validate; clear only if cold */
    trace_dump();                              /* history from BEFORE this reset */
    TRACE(TRC_BOOT, esp_reset_reason(), 0);    /* then mark the new session */

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_queue = xQueueCreate(16, sizeof(app_event_t));
    configASSERT(s_queue);

    blind_store_data_t st;
    ESP_ERROR_CHECK(blind_store_init(&st));
    ctl_init(&s_ctl, &PORTS, &CFG, &st);   /* single task so far: safe */

    motion_pins_t pins = { .gpio_step = PIN_STEP, .gpio_dir = PIN_DIR, .gpio_en = PIN_EN };
    ESP_ERROR_CHECK(motion_init(&pins, s_queue));
    motion_set_reversed(s_ctl.reversed);
    ESP_ERROR_CHECK(status_led_init(PIN_LED_EXT));
    ESP_ERROR_CHECK(keypad_init(PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_FN, s_queue));

    const esp_timer_create_args_t cal_t = { .callback = cal_timeout_cb, .name = "cal_to" };
    ESP_ERROR_CHECK(esp_timer_create(&cal_t, &s_cal_timer));
    const esp_timer_create_args_t rep_t = { .callback = report_tick_cb, .name = "report" };
    ESP_ERROR_CHECK(esp_timer_create(&rep_t, &s_report_timer));

    covering_set_queue(s_queue);
    /* The lockout flag needs no Zigbee stack — set it truthfully NOW so a
     * calibrated device that joins slowly doesn't reject remote motion in
     * the meantime; attribute sync still happens after the join wait. */
    covering_set_motion_allowed(ctl_calibrated(&s_ctl));

    zb_core_cfg_t cfg = {
        .role              = ZB_CORE_ROLE_ROUTER,
        .endpoint          = APP_ENDPOINT,
        .app_device_id     = ESP_ZB_HA_WINDOW_COVERING_DEVICE_ID,
        .manufacturer_name = MANUF_NAME,
        .model_identifier  = MODEL_ID,
        .ota = {
            .manufacturer_code = OTA_MANUFACTURER_CODE,
            .image_type        = OTA_IMAGE_TYPE,
            .file_version      = FW_VERSION_U32,
            .version_str       = FW_VERSION_STR,
        },
        .build_clusters    = covering_build_clusters,
        .post_register     = covering_post_register,
        .on_joined         = zb_network_joined_cb,
        .on_network_lost   = zb_network_lost_cb,
        .action_handler    = covering_action_handler,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));

    ESP_LOGI(TAG, "starting %s (calibrated=%d reversed=%d)",
             FW_VERSION_STR, ctl_calibrated(&s_ctl), s_ctl.reversed);

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    /* Unchanged from before the extraction; Task 8 moves this into the
     * dispatcher. */
    status_led_set(ctl_calibrated(&s_ctl) ? LED_OFF : LED_UNCAL);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    covering_report_mode(s_ctl.reversed);
    covering_report_travel_time(ctl_achieved_travel_secs(&s_ctl));
    ctl_refresh_outputs(&s_ctl);

    /* Confirm a pending-verify OTA image once the app is up (join not
     * required) or the bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
