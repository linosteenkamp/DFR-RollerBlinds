#ifndef POSITION_H
#define POSITION_H

#include <stdbool.h>
#include <stdint.h>

#define POSITION_LIFT_UNKNOWN 0xFF

typedef enum {
    POS_CAL_NONE = 0,       /* not in calibration mode */
    POS_CAL_WAIT_MARK1,     /* full calibration: awaiting Open mark */
    POS_CAL_WAIT_MARK2,     /* full calibration: awaiting Closed mark */
    POS_CAL_WAIT_REHOME,    /* re-home: span kept, awaiting single Open mark */
} pos_cal_state_t;

typedef struct {
    bool            span_valid;    /* closed_steps is trustworthy */
    bool            pos_known;     /* cur_steps is trustworthy */
    int32_t         closed_steps;  /* Span: steps from Open to Closed (>0 when valid) */
    int32_t         cur_steps;     /* current position, 0 = Open */
    pos_cal_state_t cal;
    int32_t         cal_mark1;     /* raw counter captured at mark 1 */
} position_t;

/* Restore from persisted state (blind_store) at boot. */
void position_init(position_t *p, bool span_valid, int32_t closed_steps,
                   bool pos_known, int32_t cur_steps);

/* Calibrated = span + position trusted and not currently calibrating.
 * Only a Calibrated device accepts remote motion (spec §5 lockout). */
bool position_calibrated(const position_t *p);

/* 0 (Open) .. 100 (Closed), or POSITION_LIFT_UNKNOWN. */
uint8_t position_lift_pct(const position_t *p);

/* Target step for a lift percentage, clamped to [0, closed_steps].
 * Only meaningful when calibrated. */
int32_t position_target_for_pct(const position_t *p, uint8_t pct);

/* Clamp an arbitrary target into the soft limits. */
int32_t position_clamp(const position_t *p, int32_t target);

/* Update current position after a move completes (or stops). */
void position_set_current(position_t *p, int32_t steps);

/* --- calibration lifecycle (spec §6) --- */

/* Enter calibration mode from standstill. Picks the variant automatically:
 * span kept but position unknown -> WAIT_REHOME (one mark); otherwise full
 * calibration -> WAIT_MARK1 (two marks). */
void position_cal_enter(position_t *p);

/* Record a mark at the given raw jog-counter value.
 * WAIT_MARK1: zero reference recorded -> WAIT_MARK2, returns true.
 * WAIT_MARK2: requires raw >= mark1 + min_span (Closed lies BELOW Open =
 *   higher step count). Valid: span+position set atomically, cal exits,
 *   returns true. Invalid: state unchanged (still WAIT_MARK2), returns false.
 * WAIT_REHOME: re-zeros against the kept span, cal exits, returns true.
 * POS_CAL_NONE: returns false (marks outside calibration never change state). */
bool position_cal_mark(position_t *p, int32_t raw, int32_t min_span);

/* Abort calibration (Fn long-press inside the mode, or timeout): previous
 * span/position state is untouched. */
void position_cal_abort(position_t *p);

/* Motor Reversed toggled: span AND position wiped -> uncalibrated. */
void position_wipe(position_t *p);

/* Boot detected move_in_progress flag: position no longer trusted
 * (span kept -> re-home is enough). */
void position_mark_unknown(position_t *p);

#endif /* POSITION_H */
