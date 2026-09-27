#ifndef KEYPAD_H
#define KEYPAD_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "keypad_logic.h"

/* Polls the three keys every 5 ms (buttons don't need ISRs), debounces each
 * with a key_filter integrator whose output flips only after a net 12-sample
 * excess (60 ms of a cleanly settled level, longer on a rattling line),
 * classifies via keypad_logic, and posts APP_EVT_KEYPAD events to q. */
esp_err_t keypad_init(int gpio_up, int gpio_down, int gpio_fn, QueueHandle_t q);

/* Debounced state of one key (true = pressed). One aligned int written by the
 * 5 ms poll; safe to read from the dispatcher task. */
bool keypad_key_held(key_id_t key);

#endif /* KEYPAD_H */
