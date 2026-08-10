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

/* Cruise-interval bounds for a runtime-configurable travel time.
 * The floor is a safety limit, not a hardware maximum: below it the motor is
 * expected to stall under real blind load, and a stall silently desyncs the
 * open-loop position. See
 * docs/superpowers/specs/2026-08-10-configurable-travel-time-design.md */
#define RAMP_MIN_CRUISE_US      60u
#define RAMP_MAX_CRUISE_US      1000u
#define RAMP_DEFAULT_CRUISE_US  150u

/* Convert a requested full-travel duration into a per-step cruise interval,
 * clamped to [RAMP_MIN_CRUISE_US, RAMP_MAX_CRUISE_US]. Returns
 * RAMP_DEFAULT_CRUISE_US when secs is 0 ("never set") or the span is unknown. */
uint32_t ramp_us_from_travel_time(uint16_t secs, int32_t span_steps);

/* Inverse: the duration a given cruise interval produces over span_steps.
 * Returns 0 when the span is unknown. Used to report back the value actually
 * applied, so z2m shows reality rather than the request. */
uint16_t ramp_travel_time_from_us(uint32_t cruise_us, int32_t span_steps);

/* accel_steps is clamped to total/2 (triangle profile for short moves). */
void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us,
                    uint32_t start_us, int32_t accel_steps);

/* Interval in µs for step step_idx (0-based, < total). */
uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx);

#endif /* RAMP_H */
