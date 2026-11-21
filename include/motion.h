#ifndef MOTION_H
#define MOTION_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "motion_profile.h"

typedef struct {
    int gpio_step;
    int gpio_dir;
    int gpio_en;      /* TMC2209 EN̅: high = driver DISABLED */
} motion_pins_t;

/* The item that motion posts to done_q when a move ends. It is motion's own
 * type, so that each firmware (blind, rig) can convert it to its own event. */
typedef struct {
    int32_t steps;      /* final absolute position */
    bool    completed;  /* true = reached the target; false = stopped early */
} motion_done_t;

/* Configure GPIOs (EN̅ high), create the GPTimer. motion posts a
 * motion_done_t to done_q when a move ends. Create done_q with items of
 * sizeof(motion_done_t). Use a dedicated one-slot queue: the dispatcher
 * never starts a move while one is outstanding, so the post cannot fail. */
esp_err_t motion_init(const motion_pins_t *pins, QueueHandle_t done_q);

/* Hold at idle. false (the default, the blind): the driver is disabled at
 * the end of each move. true (the rig): the driver stays enabled after a
 * move, so the TMC2209 hold current (IHOLD) holds the load.
 * A call with false while idle disables the driver immediately. A call with
 * true while idle does not enable the driver. The next move enables it.
 * Call this function only from the dispatcher task. A call from a different
 * task can race motion_start and leave the driver disabled for a move. */
void motion_set_hold_at_idle(bool hold);

/* Applied before each move; flips the DIR level meaning. Persisted elsewhere. */
void motion_set_reversed(bool reversed);

/* Absolute move from from_steps to to_steps (signed, Open=0, + toward Closed).
 * Enables the driver and runs the trapezoid. On arrival, it disables the
 * driver (unless hold at idle is set) and posts motion_done_t{completed=true}.
 * Fails with ESP_ERR_INVALID_STATE if a move is already running.
 * Equal from/to posts motion_done_t{completed=true} immediately.
 * hard_cap: any |to-from| larger is rejected (runaway watchdog, spec §6). */
esp_err_t motion_start(int32_t from_steps, int32_t to_steps,
                       const motion_profile_t *prof, int32_t hard_cap);

/* Request a decelerating stop. motion_done_t{completed=false, steps=where it
 * stopped} arrives when the motor is stationary. No-op when idle. */
void motion_stop(void);

bool motion_is_moving(void);

/* Live absolute position during a move (atomic read; between moves it equals
 * the last motion_done_t steps). Used for the 1 s progress reports. */
int32_t motion_current_steps(void);

/* Times a motion_done_t post failed (should stay 0). Read by the dispatcher. */
uint32_t motion_done_post_failures(void);

#endif /* MOTION_H */
