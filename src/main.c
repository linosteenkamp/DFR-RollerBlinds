/**
 * DFR-RollerBlinds — wiring + dispatcher. One queue; every decision happens
 * here in task context. Gesture matrix and calibration flow per CONTEXT.md /
 * spec §6; z2m motion lockout per spec §5.
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

static const char *TAG = "BLINDS";

/* ---- identity ---- */
#define MANUF_NAME  "\x0B" "DFRobot-DIY"
#define MODEL_ID    "\x10" "DFR-RollerBlinds"
#define APP_ENDPOINT 1

/* ---- GPIO map (bench-verify; see HARDWARE.md) ---- */
#define PIN_STEP     2
#define PIN_DIR      3
#define PIN_EN       4
#define PIN_BTN_UP   5
#define PIN_BTN_DOWN 6
#define PIN_BTN_FN   7
#define PIN_LED_EXT  14
#define PIN_LED_ONB  15

/* ---- motion tuning (bench constants, spec §6) ---- */
#define CRUISE_US       300      /* ~3.3 kHz at 1/8 µstep */
#define START_US        500      /* ~2 kHz first/last step */
#define ACCEL_STEPS     800
#define JOG_CRUISE_US   600      /* jog slower for control */
#define MIN_SPAN_STEPS  6000     /* ~1/4 output rev: min valid calibration */
#define HARD_CAP_MARGIN 2400     /* watchdog: allowed overshoot of span */
#define JOG_UNBOUNDED   2000000  /* "infinite" jog target while uncalibrated */
#define CAL_TIMEOUT_US  (5LL * 60 * 1000000)
#define REPORT_PERIOD_US (1LL * 1000000)

static QueueHandle_t s_queue;
static position_t    s_pos;
static bool          s_reversed;
static bool          s_cal_mode;      /* position.cal != NONE mirror for clarity */
static int32_t       s_raw;           /* raw step counter (valid in cal mode too) */
static esp_timer_handle_t s_cal_timer;
static esp_timer_handle_t s_report_timer;

static const motion_profile_t PROF_MOVE = { CRUISE_US, START_US, ACCEL_STEPS };
static const motion_profile_t PROF_JOG  = { JOG_CRUISE_US, START_US, ACCEL_STEPS };

/* ---------- helpers ---------- */

static void refresh_outputs(void)
{
    bool cal = position_calibrated(&s_pos);
    covering_set_motion_allowed(cal);
    covering_set_operational(cal);
    covering_report_lift(position_lift_pct(&s_pos));
    if (s_pos.cal == POS_CAL_WAIT_MARK1 || s_pos.cal == POS_CAL_WAIT_REHOME) {
        status_led_set(LED_CAL_MARK1);
    } else if (s_pos.cal == POS_CAL_WAIT_MARK2) {
        status_led_set(LED_CAL_MARK2);
    } else {
        status_led_set(cal ? LED_OFF : LED_UNCAL);
    }
}

static int32_t hard_cap(void)
{
    return s_pos.span_valid ? s_pos.closed_steps + HARD_CAP_MARGIN
                            : JOG_UNBOUNDED + 1;
}

static void start_move(int32_t target, const motion_profile_t *prof)
{
    blind_store_set_move_flag(true);
    esp_err_t err = motion_start(s_raw, target, prof, hard_cap());
    if (err != ESP_OK) {
        blind_store_set_move_flag(false);
        ESP_LOGW(TAG, "move refused: %s", esp_err_to_name(err));
    } else {
        esp_timer_start_periodic(s_report_timer, REPORT_PERIOD_US);
    }
}

static void goto_pct(uint8_t pct)
{
    if (!position_calibrated(&s_pos)) return;
    start_move(position_target_for_pct(&s_pos, pct), &PROF_MOVE);
}

static void jog(bool up)
{
    int32_t target;
    if (position_calibrated(&s_pos)) {
        target = up ? 0 : s_pos.closed_steps;             /* clamped jog */
    } else {
        target = up ? s_raw - JOG_UNBOUNDED : s_raw + JOG_UNBOUNDED;
    }
    start_move(target, &PROF_JOG);
}

static void cal_timeout_cb(void *arg)
{
    (void)arg;
    app_event_t ev = { .type = APP_EVT_CAL_TIMEOUT };
    xQueueSend(s_queue, &ev, 0);
}

static void report_tick_cb(void *arg)   /* esp_timer task = task context: OK */
{
    (void)arg;
    if (motion_is_moving() && position_calibrated(&s_pos)) {
        position_t tmp = s_pos;
        position_set_current(&tmp, motion_current_steps());
        covering_report_lift(position_lift_pct(&tmp));
    }
}

static void enter_or_exit_cal(void)
{
    if (motion_is_moving()) return;              /* only from standstill */
    if (s_cal_mode) {                            /* second long-press: abort */
        position_cal_abort(&s_pos);
        s_cal_mode = false;
        esp_timer_stop(s_cal_timer);
    } else {
        position_cal_enter(&s_pos);
        s_cal_mode = true;
        s_raw = s_pos.pos_known ? s_pos.cur_steps : 0;   /* fresh raw frame */
        esp_timer_start_once(s_cal_timer, CAL_TIMEOUT_US);
    }
    refresh_outputs();
}

static void handle_mark(void)
{
    if (motion_is_moving()) { motion_stop(); return; }   /* Fn tap = stop first */
    if (!s_cal_mode) return;                             /* idle taps inert */
    pos_cal_state_t before = s_pos.cal;
    if (position_cal_mark(&s_pos, s_raw, MIN_SPAN_STEPS)) {
        if (s_pos.cal == POS_CAL_NONE) {                 /* calibration finished */
            s_cal_mode = false;
            esp_timer_stop(s_cal_timer);
            blind_store_save_span(s_pos.span_valid, s_pos.closed_steps);
            blind_store_save_position(s_pos.pos_known, s_pos.cur_steps);
            s_raw = s_pos.cur_steps;                     /* re-anchor raw frame */
        } else if (before == POS_CAL_WAIT_MARK1) {
            /* mark 1 accepted: re-zero the raw frame at Open */
            s_raw = 0;
        }
        status_led_flash(LED_ACK);
    } else {
        status_led_flash(LED_ERROR);                     /* stay awaiting mark 2 */
    }
    refresh_outputs();
}

static void toggle_reversed(void)
{
    if (motion_is_moving()) return;
    s_reversed = !s_reversed;
    motion_set_reversed(s_reversed);
    blind_store_save_motor_reversed(s_reversed);
    /* direction sense changed -> all stored steps are meaningless (spec §6) */
    position_wipe(&s_pos);
    if (s_cal_mode) { s_cal_mode = false; esp_timer_stop(s_cal_timer); }
    blind_store_save_span(false, 0);
    blind_store_save_position(false, 0);
    s_raw = 0;
    covering_report_mode(s_reversed);
    status_led_flash(LED_ACK);
    refresh_outputs();
}

/* ---------- event dispatch ---------- */

static void handle_keypad(kp_event_t e)
{
    bool cal_dev = position_calibrated(&s_pos);
    switch (e.type) {
    case KP_EVT_TAP:
        if (motion_is_moving()) { motion_stop(); break; }   /* any tap stops */
        if (e.key == KEY_FN) { handle_mark(); break; }
        if (cal_dev) {                                       /* full travel */
            goto_pct(e.key == KEY_UP ? 0 : 100);
        }                                                    /* uncal: inert */
        break;
    case KP_EVT_HOLD_START:
        if (!motion_is_moving()) jog(e.key == KEY_UP);
        break;
    case KP_EVT_HOLD_END:
        motion_stop();
        break;
    case KP_EVT_FN_LONG:
        enter_or_exit_cal();
        break;
    case KP_EVT_CHORD_REVERSE:
        toggle_reversed();
        break;
    default:
        break;
    }
}

static void dispatcher_task(void *pv)
{
    (void)pv;
    app_event_t ev;
    for (;;) {
        if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.type) {
        case APP_EVT_KEYPAD:
            handle_keypad(ev.kp);
            break;
        case APP_EVT_ZB_OPEN:   if (!motion_is_moving()) goto_pct(0);   break;
        case APP_EVT_ZB_CLOSE:  if (!motion_is_moving()) goto_pct(100); break;
        case APP_EVT_ZB_GOTO:   if (!motion_is_moving()) goto_pct(ev.pct); break;
        case APP_EVT_ZB_STOP:   motion_stop(); break;
        case APP_EVT_ZB_SET_REVERSED:
            if (ev.on != s_reversed) toggle_reversed();
            break;
        case APP_EVT_MOTION_DONE:
            esp_timer_stop(s_report_timer);
            s_raw = ev.steps;
            if (!s_cal_mode) {
                position_set_current(&s_pos, position_clamp(&s_pos, ev.steps));
                blind_store_save_position(s_pos.pos_known, s_pos.cur_steps);
            }
            blind_store_set_move_flag(false);
            refresh_outputs();
            break;
        case APP_EVT_CAL_TIMEOUT:
            if (s_cal_mode) {
                if (motion_is_moving()) motion_stop();
                position_cal_abort(&s_pos);
                s_cal_mode = false;
                refresh_outputs();
            }
            break;
        default:
            break;
        }
    }
}

/* ---------- boot ---------- */

void app_main(void)
{
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
    position_init(&s_pos, st.span_valid, st.closed_steps, st.pos_known, st.cur_steps);
    if (st.move_in_progress) {
        /* power died mid-move: position no longer trusted (spec §6) */
        position_mark_unknown(&s_pos);
        blind_store_save_position(false, 0);
        blind_store_set_move_flag(false);
        ESP_LOGW(TAG, "unclean shutdown mid-move -> Position Unknown, re-home needed");
    }
    s_reversed = st.motor_reversed;
    s_raw = s_pos.pos_known ? s_pos.cur_steps : 0;

    motion_pins_t pins = { .gpio_step = PIN_STEP, .gpio_dir = PIN_DIR, .gpio_en = PIN_EN };
    ESP_ERROR_CHECK(motion_init(&pins, s_queue));
    motion_set_reversed(s_reversed);
    ESP_ERROR_CHECK(status_led_init(PIN_LED_EXT, PIN_LED_ONB));
    ESP_ERROR_CHECK(keypad_init(PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_FN, s_queue));

    const esp_timer_create_args_t cal_t = { .callback = cal_timeout_cb, .name = "cal_to" };
    ESP_ERROR_CHECK(esp_timer_create(&cal_t, &s_cal_timer));
    const esp_timer_create_args_t rep_t = { .callback = report_tick_cb, .name = "report" };
    ESP_ERROR_CHECK(esp_timer_create(&rep_t, &s_report_timer));

    covering_set_queue(s_queue);
    covering_set_motion_allowed(false);   /* until refresh after join */

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
        .on_joined         = NULL,   /* initial attribute sync happens below */
        .action_handler    = covering_action_handler,
    };
    ESP_ERROR_CHECK(zb_core_init(&cfg));

    xTaskCreate(dispatcher_task, "dispatcher", 4096, NULL, 6, NULL);

    ESP_LOGI(TAG, "starting %s (calibrated=%d reversed=%d)",
             FW_VERSION_STR, position_calibrated(&s_pos), s_reversed);
    if (zb_core_wait_ready(60000)) {
        ESP_LOGI(TAG, "joined");
    }
    covering_report_mode(s_reversed);
    refresh_outputs();

    /* Confirm a pending-verify OTA image once the app is up (join not
     * required) or the bootloader rolls back on the next reset. */
    ota_client_mark_valid();
}
