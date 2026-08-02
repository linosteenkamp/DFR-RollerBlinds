#ifndef MOTION_H
#define MOTION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    int gpio_step;
    int gpio_dir;
    int gpio_en;      /* TMC2209 EN̅: high = driver DISABLED */
} motion_pins_t;

typedef struct {
    uint32_t cruise_us;   /* step interval at cruise (e.g. 300 = ~3.3 kHz) */
    uint32_t start_us;    /* first/last-step interval (e.g. 500 = 2 kHz) */
    int32_t  accel_steps; /* ramp length in steps (e.g. 800) */
} motion_profile_t;

/* Configure GPIOs (EN̅ high), create the GPTimer. done-events are posted to q
 * as APP_EVT_MOTION_DONE {steps=final absolute position, completed}. */
esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t q);

/* Applied before each move; flips the DIR level meaning. Persisted elsewhere. */
void motion_set_reversed(bool reversed);

/* Absolute move from from_steps to to_steps (signed, Open=0, + toward Closed).
 * Enables the driver, runs the trapezoid, disables the driver on arrival,
 * posts MOTION_DONE{completed=true}. Fails with ESP_ERR_INVALID_STATE if a
 * move is already running; equal from/to posts MOTION_DONE immediately.
 * hard_cap: any |to-from| larger is rejected (runaway watchdog, spec §6). */
esp_err_t motion_start(int32_t from_steps, int32_t to_steps,
                       const motion_profile_t *prof, int32_t hard_cap);

/* Request a decelerating stop. MOTION_DONE{completed=false, steps=where it
 * stopped} arrives when the motor is stationary. No-op when idle. */
void motion_stop(void);

bool motion_is_moving(void);

/* Live absolute position during a move (atomic read; between moves it equals
 * the last MOTION_DONE steps). Used for the 1 s progress reports. */
int32_t motion_current_steps(void);

#endif /* MOTION_H */
