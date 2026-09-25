#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "esp_err.h"
#include "led_pattern.h"

esp_err_t status_led_init(int gpio_ext);
void status_led_set(led_pattern_t base);       /* persistent */
void status_led_flash(led_pattern_t transient);/* LED_ACK / LED_ERROR overlay */

#endif /* STATUS_LED_H */
