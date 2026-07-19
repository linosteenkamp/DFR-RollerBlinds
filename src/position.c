/**
 * @file position.c
 * @brief Pure position/calibration logic. No ESP dependencies — host-testable.
 *        Steps are signed; 0 = Open, increasing toward Closed (after the
 *        motor_reversed setting is applied by the motion layer).
 */
#include "position.h"

void position_init(position_t *p, bool span_valid, int32_t closed_steps,
                   bool pos_known, int32_t cur_steps)
{
    p->span_valid   = span_valid && closed_steps > 0;
    p->closed_steps = p->span_valid ? closed_steps : 0;
    p->pos_known    = pos_known && p->span_valid;
    p->cur_steps    = p->pos_known ? cur_steps : 0;
    p->cal          = POS_CAL_NONE;
    p->cal_mark1    = 0;
}

bool position_calibrated(const position_t *p)
{
    return p->span_valid && p->pos_known && p->cal == POS_CAL_NONE;
}

uint8_t position_lift_pct(const position_t *p)
{
    if (!position_calibrated(p)) {
        return POSITION_LIFT_UNKNOWN;
    }
    /* round to nearest percent */
    int64_t pct = ((int64_t)p->cur_steps * 100 + p->closed_steps / 2) / p->closed_steps;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

int32_t position_clamp(const position_t *p, int32_t target)
{
    if (!p->span_valid) {
        return target;
    }
    if (target < 0) return 0;
    if (target > p->closed_steps) return p->closed_steps;
    return target;
}

int32_t position_target_for_pct(const position_t *p, uint8_t pct)
{
    if (pct > 100) pct = 100;
    return position_clamp(p, (int32_t)(((int64_t)p->closed_steps * pct) / 100));
}

void position_set_current(position_t *p, int32_t steps)
{
    p->cur_steps = steps;
}

void position_cal_enter(position_t *p)
{
    p->cal = (p->span_valid && !p->pos_known) ? POS_CAL_WAIT_REHOME
                                              : POS_CAL_WAIT_MARK1;
}

bool position_cal_mark(position_t *p, int32_t raw, int32_t min_span)
{
    switch (p->cal) {
    case POS_CAL_WAIT_MARK1:
        p->cal_mark1 = raw;
        p->cal = POS_CAL_WAIT_MARK2;
        return true;
    case POS_CAL_WAIT_MARK2: {
        int32_t span = raw - p->cal_mark1;
        /* Closed must lie below Open by at least min_span (down = raw
         * increase); span must also be positive even if a caller ever passes
         * min_span <= 0 — a zero span would make lift math divide by zero. */
        if (span < min_span || span < 1) {
            return false;   /* rejected; stay in WAIT_MARK2 */
        }
        /* Atomic commit: span + position together, then exit the mode. */
        p->closed_steps = span;
        p->span_valid   = true;
        p->cur_steps    = p->closed_steps;   /* physically at Closed */
        p->pos_known    = true;
        p->cal          = POS_CAL_NONE;
        return true;
    }
    case POS_CAL_WAIT_REHOME:
        p->cur_steps = 0;                    /* physically at Open */
        p->pos_known = true;
        p->cal       = POS_CAL_NONE;
        return true;
    case POS_CAL_NONE:
    default:
        return false;                        /* idle marks are inert */
    }
}

void position_cal_abort(position_t *p)
{
    p->cal = POS_CAL_NONE;
}

void position_wipe(position_t *p)
{
    p->span_valid   = false;
    p->pos_known    = false;
    p->closed_steps = 0;
    p->cur_steps    = 0;
    p->cal          = POS_CAL_NONE;
}

void position_mark_unknown(position_t *p)
{
    p->pos_known = false;
}
