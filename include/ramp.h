#ifndef RAMP_H
#define RAMP_H

#include <stdint.h>

/* Trapezoidal (or, for short moves, triangular) speed profile expressed as a
 * per-step timer interval. Speeds interpolate linearly in the FREQUENCY
 * domain between 1e6/start_us and 1e6/cruise_us over accel_steps, mirror-image
 * on deceleration. Pure math: the motion ISR asks for the interval of the
 * step it is about to schedule. */
typedef struct {
    int32_t  total;        /* total steps in the move (> 0) */
    int32_t  accel_steps;  /* steps in the accel phase (== decel phase) */
    uint32_t start_us;     /* interval of the first/last step (slowest) */
    uint32_t cruise_us;    /* interval at cruise (fastest) */
} ramp_plan_t;

/* accel_steps is clamped to total/2 (triangle profile for short moves). */
void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us,
                    uint32_t start_us, int32_t accel_steps);

/* Interval in µs for step step_idx (0-based, < total). */
uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx);

#endif /* RAMP_H */
