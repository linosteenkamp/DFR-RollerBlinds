#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"

/* Spec §2 LED patterns. Base patterns persist; transient patterns
 * (ACK/ERROR) play once and revert to the base. */
typedef enum {
    LED_OFF = 0,        /* normal: calibrated, idle */
    LED_CAL_MARK1,      /* 1 Hz blink: awaiting mark 1 (Open) */
    LED_CAL_MARK2,      /* 4 Hz blink: awaiting mark 2 (Closed) */
    LED_UNCAL,          /* double-flash every 3 s: uncalibrated / pos unknown */
    LED_IDENTIFY,       /* steady rapid blink: Zigbee Identify */
    LED_ACK,            /* transient: three quick flashes */
    LED_ERROR,          /* transient: five rapid flashes */
} led_pattern_t;

esp_err_t status_led_init(int gpio_ext, int gpio_onboard);
void status_led_set(led_pattern_t base);       /* persistent */
void status_led_flash(led_pattern_t transient);/* LED_ACK / LED_ERROR overlay */

#endif /* STATUS_LED_H */
