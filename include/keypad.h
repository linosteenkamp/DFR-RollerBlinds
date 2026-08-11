#ifndef KEYPAD_H
#define KEYPAD_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Polls the three keys every 5 ms (buttons don't need ISRs), debounces each
 * with a key_filter integrator requiring 60 ms of settled level per edge,
 * classifies via keypad_logic, and posts APP_EVT_KEYPAD events to q. */
esp_err_t keypad_init(int gpio_up, int gpio_down, int gpio_fn, QueueHandle_t q);

#endif /* KEYPAD_H */
