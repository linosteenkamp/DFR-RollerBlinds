/**
 * @file ramp.c
 * @brief Pure trapezoid/triangle profile math. Called from the motion ISR —
 *        keep it allocation-free and branch-light. (The motion module places
 *        this in IRAM via its own compilation unit copy of the hot path; this
 *        file stays pure C for host tests.)
 */
#include "ramp.h"

void ramp_plan_init(ramp_plan_t *r, int32_t total_steps, uint32_t cruise_us,
                    uint32_t start_us, int32_t accel_steps)
{
    if (total_steps < 1) total_steps = 1;
    if (accel_steps < 1) accel_steps = 1;
    if (accel_steps > total_steps / 2) accel_steps = total_steps / 2;
    if (accel_steps < 1) accel_steps = 1;   /* total==1 edge */
    r->total       = total_steps;
    r->accel_steps = accel_steps;
    r->start_us    = start_us;
    r->cruise_us   = cruise_us;
}

/* Linear interpolation in the frequency domain between f0=1e6/start and
 * fc=1e6/cruise: f(i) = f0 + (fc-f0)*i/accel. Returns 1e6/f(i). */
static uint32_t interval_at(const ramp_plan_t *r, int32_t i_into_ramp)
{
    if (i_into_ramp >= r->accel_steps) {
        return r->cruise_us;
    }
    uint32_t f0 = 1000000u / r->start_us;
    uint32_t fc = 1000000u / r->cruise_us;
    uint32_t f  = f0 + (uint32_t)(((uint64_t)(fc - f0) * (uint32_t)i_into_ramp)
                                  / (uint32_t)r->accel_steps);
    return 1000000u / f;
}

uint32_t ramp_interval_us(const ramp_plan_t *r, int32_t step_idx)
{
    if (step_idx < 0) step_idx = 0;
    if (step_idx >= r->total) step_idx = r->total - 1;
    int32_t from_end = r->total - 1 - step_idx;
    int32_t i = (step_idx < from_end) ? step_idx : from_end;  /* mirror */
    return interval_at(r, i);
}
