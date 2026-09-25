#ifndef CTL_H
#define CTL_H

/* Dispatcher core. Every decision about motion, calibration and persistence
 * happens here, one app_event_t at a time. Pure C with no ESP-IDF headers so
 * it runs in host tests: every side effect goes through ctl_ports_t, which
 * main.c binds to the real modules and the tests bind to a recording fake.
 *
 * Concurrency: only the dispatcher task may call ctl_handle(), and ctl calls
 * nothing but its ports and the pure position/ramp modules. */

#include <stdbool.h>
#include <stdint.h>
#include "app_event.h"
#include "blind_store_data.h"
#include "keypad_logic.h"
#include "led_pattern.h"
#include "motion_profile.h"
#include "position.h"

typedef int ctl_err_t;   /* 0 = OK; carries esp_err_t values unchanged */
#define CTL_OK 0

typedef struct {
    /* motion */
    ctl_err_t (*motion_start)(int32_t from_steps, int32_t to_steps,
                              const motion_profile_t *prof, int32_t hard_cap);
    void      (*motion_stop)(void);
    bool      (*motion_is_moving)(void);   /* removed in Task 5 */
    int32_t   (*motion_steps)(void);
    void      (*motion_set_reversed)(bool reversed);
    /* persistence (NVS namespace "blind") */
    ctl_err_t (*save_move_flag)(bool in_progress);
    ctl_err_t (*save_span)(bool span_valid, int32_t closed_steps);
    ctl_err_t (*save_position)(bool pos_known, int32_t cur_steps);
    ctl_err_t (*save_reversed)(bool reversed);
    ctl_err_t (*save_travel_time)(uint16_t secs);
    /* Zigbee */
    void (*set_motion_allowed)(bool allowed);
    void (*set_operational)(bool calibrated);
    void (*report_lift)(uint8_t pct);
    void (*report_mode)(bool reversed);
    void (*report_travel_time)(uint16_t secs);
    bool (*net_joined)(void);
    void (*factory_reset)(void);
    /* operator */
    void (*led_set)(led_pattern_t base);
    void (*led_flash)(led_pattern_t transient);
    /* timers */
    void (*cal_timer_start)(void);
    void (*cal_timer_stop)(void);
    void (*report_timer_start)(void);
    void (*report_timer_stop)(void);
    /* diagnostics */
    void (*trace)(uint16_t code, int32_t a, int32_t b);
    void (*log)(const char *msg);
} ctl_ports_t;

typedef struct {
    motion_profile_t move;   /* travel profile; cruise_us recomputed from travel time */
    motion_profile_t jog;
    int32_t min_span_steps;
    int32_t hard_cap_margin;
    int32_t jog_unbounded;
} ctl_config_t;

typedef struct {
    const ctl_ports_t *io;
    ctl_config_t cfg;          /* cfg.move.cruise_us is live */
    position_t pos;
    int32_t  raw;              /* raw step frame; valid in Calibration Mode too */
    bool     reversed;
    uint16_t travel_secs;      /* requested full-travel time; 0 = unset */
    bool     identifying;      /* Zigbee Identify in progress */
    bool     cal_moved;        /* jogged during this Calibration Mode session */
    bool     cal_abort_pending;/* timeout arrived mid-move: abort on MOTION_DONE */
    bool     pending_valid;    /* Zigbee goto parked while a move decelerates */
    uint8_t  pending_pct;
    uint8_t  move_start_pct;   /* lift % when the current move began */
} ctl_t;

void ctl_init(ctl_t *c, const ctl_ports_t *io, const ctl_config_t *cfg,
              const blind_store_data_t *boot);
void ctl_handle(ctl_t *c, const app_event_t *ev);

bool          ctl_calibrated(const ctl_t *c);
bool          ctl_in_cal_mode(const ctl_t *c);
led_pattern_t ctl_led_pattern(const ctl_t *c);
uint16_t      ctl_achieved_travel_secs(const ctl_t *c);

/* Pushes lockout, ConfigStatus, lift and the LED base pattern. Public only
 * while app_main still does the post-join sync itself (removed in Task 8). */
void ctl_refresh_outputs(ctl_t *c);

#endif /* CTL_H */
