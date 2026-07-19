#ifndef APP_EVENT_H
#define APP_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include "keypad_logic.h"

/* One queue, one item type. Producers: keypad ISR-side task (KP_*), the
 * Zigbee action handler via covering (ZB_*), the motion ISR (MOTION_DONE),
 * and esp_timer callbacks (CAL_TIMEOUT). Consumer: the dispatcher in main. */
typedef enum {
    APP_EVT_KEYPAD = 0,      /* .kp: gesture from keypad_logic */
    APP_EVT_ZB_OPEN,         /* UpOpen command */
    APP_EVT_ZB_CLOSE,        /* DownClose command */
    APP_EVT_ZB_STOP,         /* Stop command */
    APP_EVT_ZB_GOTO,         /* .pct: GoToLiftPercentage */
    APP_EVT_ZB_SET_REVERSED, /* .on: Mode attr bit0 written from z2m */
    APP_EVT_MOTION_DONE,     /* .steps final position, .completed reached target */
    APP_EVT_CAL_TIMEOUT,     /* 5-min calibration timeout */
    APP_EVT_REPORT_TICK,     /* 1 s live-position reporting tick during moves */
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    union {
        kp_event_t kp;
        uint8_t    pct;
        bool       on;
        struct { int32_t steps; bool completed; };
    };
} app_event_t;

#endif /* APP_EVENT_H */
