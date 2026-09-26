/**
 * @file ctl.c
 * @brief Dispatcher core. Every decision the firmware makes about motion,
 *        calibration and persistence happens here, one app_event_t at a time,
 *        on the dispatcher task. Gesture matrix and calibration flow per
 *        CONTEXT.md / spec §6; z2m motion lockout per spec §5. Pure: every
 *        side effect goes through c->io.
 */
#include "ctl.h"
#include "ramp.h"
#include "trace.h"

static bool in_cal(const ctl_t *c) { return c->pos.cal != POS_CAL_NONE; }

static bool moving(const ctl_t *c) { return c->move_active; }

bool ctl_calibrated(const ctl_t *c)  { return position_calibrated(&c->pos); }
bool ctl_in_cal_mode(const ctl_t *c) { return in_cal(c); }

led_pattern_t ctl_led_pattern(const ctl_t *c)
{
    if (c->identifying) return LED_IDENTIFY;
    if (c->pos.cal == POS_CAL_WAIT_MARK1 || c->pos.cal == POS_CAL_WAIT_REHOME) {
        return LED_CAL_MARK1;
    }
    if (c->pos.cal == POS_CAL_WAIT_MARK2) return LED_CAL_MARK2;
    if (!c->io->net_joined()) return LED_NO_NETWORK;
    return ctl_calibrated(c) ? LED_OFF : LED_UNCAL;
}

void ctl_refresh_outputs(ctl_t *c)
{
    bool cal = ctl_calibrated(c);
    c->io->set_motion_allowed(cal);
    c->io->set_operational(cal);
    c->io->report_lift(position_lift_pct(&c->pos));
    c->io->led_set(ctl_led_pattern(c));
}

/* ---------- travel time ---------- */

/* Derive the cruise interval from the requested travel time and the current
 * span (clamped). Read at move start, so a change mid-move applies to the
 * NEXT move. */
static void recompute_cruise(ctl_t *c)
{
    c->cfg.move.cruise_us = ramp_us_from_travel_time(c->travel_secs,
                                                     c->pos.closed_steps);
}

/* The duration the current cruise interval actually produces. With no span
 * there is nothing to clamp against, so the request stands unchanged;
 * calibration will correct it. */
uint16_t ctl_achieved_travel_secs(const ctl_t *c)
{
    return (c->pos.closed_steps > 0)
        ? ramp_travel_time_from_us(c->cfg.move.cruise_us, c->pos.closed_steps)
        : c->travel_secs;
}

/* Recompute the cruise interval and tell z2m what was actually applied.
 * Called on a z2m write and whenever the span changes — the stored value is
 * a duration, so its meaning moves with the span. */
static void apply_travel_time(ctl_t *c)
{
    recompute_cruise(c);
    c->io->report_travel_time(ctl_achieved_travel_secs(c));
}

/* ---------- moves ---------- */

static int32_t hard_cap(const ctl_t *c)
{
    /* Watchdog cap applies to calibrated moves only. Every state where the
     * operator jogs with a deadman hold (uncalibrated, Position Unknown,
     * calibration mode) must be uncapped — matching jog()'s target choice —
     * or Re-home/recal jogs would be refused while span_valid is still set. */
    return ctl_calibrated(c) ? c->pos.closed_steps + c->cfg.hard_cap_margin
                             : c->cfg.jog_unbounded + 1;
}

static bool start_move(ctl_t *c, int32_t target, const motion_profile_t *prof)
{
    if (target == c->raw) {
        c->io->trace(TRC_MOVE_NOOP, target, c->pos.closed_steps);
        c->io->led_flash(LED_ACK);   /* heard you; already there */
        ctl_refresh_outputs(c);      /* keep reports honest, no NVS churn */
        return false;
    }
    int32_t cap = hard_cap(c);       /* hoisted so a refusal can record it */
    c->move_start_pct = position_lift_pct(&c->pos);
    c->io->trace(TRC_MOVE_START, c->raw, target);
    /* Without the flag a power cut mid-move would boot Calibrated with a
     * wrong position — exactly what the flag exists to prevent. */
    if (c->io->save_move_flag(true) != CTL_OK) {
        c->io->trace(TRC_MOVE_REFUSED, -1, cap);
        c->io->led_flash(LED_ERROR);
        c->io->log("move refused: move flag not saved");
        return false;
    }
    ctl_err_t err = c->io->motion_start(c->raw, target, prof, cap);
    if (err != CTL_OK) {
        c->io->save_move_flag(false);
        c->io->trace(TRC_MOVE_REFUSED, err, cap);
        c->io->led_flash(LED_ERROR);  /* refusing — needs attention */
        c->io->log("move refused");
        return false;
    }
    c->move_active = true;
    c->io->report_timer_start();
    return true;
}

static void goto_pct(ctl_t *c, uint8_t pct)
{
    if (!ctl_calibrated(c)) return;
    (void)start_move(c, position_target_for_pct(&c->pos, pct), &c->cfg.move);
}

static void jog(ctl_t *c, key_id_t key)
{
    bool up = (key == KEY_UP);
    int32_t target;
    if (ctl_calibrated(c)) {
        target = up ? 0 : c->pos.closed_steps;              /* clamped jog */
    } else {
        target = up ? c->raw - c->cfg.jog_unbounded
                    : c->raw + c->cfg.jog_unbounded;
    }
    if (start_move(c, target, &c->cfg.jog)) {
        c->jog_active    = true;
        c->jog_key       = key;
        c->jog_stop_sent = false;
    }
}

/* Hold-to-jog must stop on release even if HOLD_END never arrives (a full
 * queue drops it). Checked after every event: a full queue means the
 * dispatcher is behind, so it has events to process and catches the release
 * within one of them; the 1 s report tick is the backstop. */
static void deadman(ctl_t *c)
{
    if (c->jog_active && !c->jog_stop_sent && !c->io->key_held(c->jog_key)) {
        c->jog_stop_sent = true;
        c->io->trace(TRC_DEADMAN_STOP, c->jog_key, 0);
        c->io->motion_stop();
    }
}

/* ---------- calibration (spec §6) ---------- */

/* Leave Calibration Mode without a result. The span stays untouched, but if
 * the blind was jogged while in the mode the stored position no longer
 * matches reality — drop to Position Unknown so a Re-home is demanded
 * instead of trusting stale state. */
static void cal_abort(ctl_t *c)
{
    c->cal_abort_pending = false;
    position_cal_abort(&c->pos);
    c->io->cal_timer_stop();
    if (c->cal_moved && c->pos.span_valid) {
        position_mark_unknown(&c->pos);
        if (c->io->save_position(false, 0) != CTL_OK) {
            /* the session's first jog already saved the position untrusted,
             * so flash is on the safe side; record it and carry on */
            c->io->log("abort: position save failed (already untrusted on flash)");
        }
    }
}

static void enter_or_exit_cal(ctl_t *c)
{
    if (moving(c)) return;                     /* only from standstill */
    if (in_cal(c)) {                           /* second long-press: abort */
        cal_abort(c);
    } else {
        position_cal_enter(&c->pos);
        c->cal_moved = false;
        c->cal_abort_pending = false;
        c->raw = c->pos.pos_known ? c->pos.cur_steps : 0;   /* fresh raw frame */
        c->io->cal_timer_start();
    }
    ctl_refresh_outputs(c);
}

static void handle_mark(ctl_t *c)
{
    if (moving(c)) { c->io->motion_stop(); return; }   /* Fn tap = stop first */
    if (!in_cal(c)) {
        c->io->led_flash(LED_ACK);                      /* heard you; nothing to mark */
        return;
    }
    /* raw stays one continuous frame through the whole calibration —
     * position_cal_mark stores mark 1's raw and computes the span as the
     * difference at mark 2, so the caller must NOT re-anchor between marks. */
    position_t before = c->pos;
    if (position_cal_mark(&c->pos, c->raw, c->cfg.min_span_steps)) {
        if (!in_cal(c)) {                               /* calibration finished */
            /* Span first: if the position save then fails, flash holds the
             * new span with the position the session's first jog already
             * saved as untrusted — a Re-home, never a wrong Calibrated. */
            if (c->io->save_span(c->pos.span_valid, c->pos.closed_steps) != CTL_OK ||
                c->io->save_position(c->pos.pos_known, c->pos.cur_steps) != CTL_OK) {
                c->pos = before;                        /* treat as a rejected mark */
                c->io->led_flash(LED_ERROR);
                c->io->log("calibration not saved: mark rejected, try again");
                ctl_refresh_outputs(c);
                return;
            }
            c->io->cal_timer_stop();
            c->raw = c->pos.cur_steps;                  /* re-anchor raw frame */
            apply_travel_time(c);   /* new span -> same duration, new interval */
        }
        c->io->led_flash(LED_ACK);
    } else {
        c->io->led_flash(LED_ERROR);                    /* stay awaiting mark 2 */
    }
    ctl_refresh_outputs(c);
}

/* Invalidate before flipping: if power is lost between the saves, flash must
 * hold "uncalibrated", never the new direction with the old calibration —
 * that would drive past the limits with full confidence (spec §6). Flash
 * never trusts more than RAM. */
static void toggle_reversed(ctl_t *c)
{
    if (moving(c)) return;
    if (c->io->save_span(false, 0) != CTL_OK ||
        c->io->save_position(false, 0) != CTL_OK) {
        c->io->led_flash(LED_ERROR);
        c->io->log("motor_reversed refused: calibration could not be invalidated");
        c->io->report_mode(c->reversed);    /* z2m sees the unchanged truth */
        return;
    }
    bool was_cal = in_cal(c);
    position_wipe(&c->pos);                 /* direction changes -> steps meaningless */
    if (was_cal) c->io->cal_timer_stop();
    c->raw = 0;
    bool want = !c->reversed;
    if (c->io->save_reversed(want) == CTL_OK) {
        c->reversed = want;
        c->io->motion_set_reversed(want);
        c->io->led_flash(LED_ACK);
    } else {
        /* calibration is gone either way; the old direction is the safe one */
        c->io->led_flash(LED_ERROR);
        c->io->log("motor_reversed not saved: direction unchanged");
    }
    c->io->report_mode(c->reversed);
    apply_travel_time(c);   /* span was wiped: fall back until recalibrated */
    ctl_refresh_outputs(c);
}

/* ---------- Zigbee ---------- */

/* Spec §7: a command during a move preempts — decelerate to stop, then run
 * the new target (last writer wins). The target is parked until MOTION_DONE. */
static void zb_goto_request(ctl_t *c, uint8_t pct)
{
    if (!ctl_calibrated(c)) return;   /* lockout backstop */
    bool mv = moving(c);
    c->io->trace(TRC_ZB_CMD, pct, mv ? 1 : 0);
    if (mv) {
        c->pending_pct   = pct;
        c->pending_valid = true;
        if (c->jog_active) c->jog_stop_sent = true;
        c->io->motion_stop();
    } else {
        goto_pct(c, pct);
    }
}

/* ---------- events ---------- */

static void handle_keypad(ctl_t *c, kp_event_t e)
{
    c->pending_valid = false;   /* any local input is the last writer (spec §7) */
    bool cal_dev = ctl_calibrated(c);
    /* Calibration state folded into the record (0x100 bit): a lockout
     * early-return downstream is otherwise indistinguishable from a
     * dispatcher that silently decided nothing. */
    c->io->trace(TRC_KEY_EVENT, e.type, e.key | (cal_dev ? 0x100 : 0));
    switch (e.type) {
    case KP_EVT_TAP:
        if (moving(c)) { c->io->motion_stop(); break; }   /* any tap stops */
        if (e.key == KEY_FN) { handle_mark(c); break; }
        if (cal_dev) {                                     /* full travel */
            goto_pct(c, e.key == KEY_UP ? 0 : 100);
        } else if (!in_cal(c)) {
            /* Inside Calibration Mode a short Up/Down tap stays quiet:
             * LED_ERROR there already means "mark rejected", and firing it
             * here too could send the operator jogging the wrong way. The
             * lockout signal only applies outside the mode. */
            c->io->led_flash(LED_ERROR);   /* lockout: needs calibration */
        }
        break;
    case KP_EVT_HOLD_START:
        if (!moving(c)) jog(c, e.key);
        break;
    case KP_EVT_HOLD_END:
        if (c->jog_active) c->jog_stop_sent = true;
        c->io->motion_stop();
        break;
    case KP_EVT_FN_LONG:
        enter_or_exit_cal(c);
        break;
    /* KP_EVT_CHORD_REVERSE is retired and never emitted; motor_reversed is a
     * z2m setting (APP_EVT_ZB_SET_REVERSED). */
    case KP_EVT_FACTORY_RESET:
        /* Never mid-move: the reset reboots, and rebooting with
         * move_in_progress still set drops the device to Position Unknown. */
        if (moving(c)) {
            c->io->log("factory reset ignored: stop the blind first");
            break;
        }
        c->io->log("keypad factory reset: erasing Zigbee state");
        c->io->factory_reset();   /* does not return on the device */
        break;
    default:
        break;
    }
}

static void on_motion_done(ctl_t *c, int32_t steps, bool completed)
{
    c->move_active = false;   /* the only place it clears */
    c->jog_active  = false;
    c->io->trace(TRC_MOVE_DONE, steps, completed);
    c->io->report_timer_stop();
    c->raw = steps;
    ctl_err_t perr = CTL_OK;
    if (in_cal(c)) {
        if (!c->cal_moved) {
            /* first jog of this session: position on disk is now stale —
             * persist untrusted so a power blip can't boot back into a
             * confidently wrong Calibrated state */
            perr = c->io->save_position(false, 0);
        }
        c->cal_moved = true;
        if (c->cal_abort_pending) cal_abort(c);   /* timeout hit mid-jog */
    } else {
        position_set_current(&c->pos, position_clamp(&c->pos, steps));
        perr = c->io->save_position(c->pos.pos_known, c->pos.cur_steps);
        /* Dead-zone feedback: a move too small to change the reported lift %
         * is invisible to the operator and to z2m alike, so it is
         * indistinguishable from a dead key unless we say so. */
        if (ctl_calibrated(c) &&
            position_lift_pct(&c->pos) == c->move_start_pct) {
            c->io->led_flash(LED_ACK);
        }
    }
    if (perr == CTL_OK) {
        c->io->save_move_flag(false);
    } else {
        /* leaving the flag set forces a re-home next boot rather than
         * trusting a position that failed to persist */
        c->io->log("position save failed: move flag left set, re-home on next boot");
    }
    if (c->pending_valid && !in_cal(c) && ctl_calibrated(c)) {
        uint8_t pct = c->pending_pct;   /* ZB preemption: last writer */
        c->pending_valid = false;
        goto_pct(c, pct);
    }
    ctl_refresh_outputs(c);
}

void ctl_handle(ctl_t *c, const app_event_t *ev)
{
    switch (ev->type) {
    case APP_EVT_KEYPAD:   handle_keypad(c, ev->kp);      break;
    case APP_EVT_ZB_OPEN:  zb_goto_request(c, 0);         break;
    case APP_EVT_ZB_CLOSE: zb_goto_request(c, 100);       break;
    case APP_EVT_ZB_GOTO:  zb_goto_request(c, ev->pct);   break;
    case APP_EVT_ZB_STOP:
        c->pending_valid = false;
        if (c->jog_active) c->jog_stop_sent = true;
        c->io->motion_stop();
        break;
    case APP_EVT_ZB_SET_REVERSED:
        if (moving(c)) {
            c->io->report_mode(c->reversed);   /* reject: rewrite truth */
        } else if (ev->on != c->reversed) {
            toggle_reversed(c);
        }
        break;
    case APP_EVT_ZB_SET_SPEED:
        c->travel_secs = ev->secs;
        c->io->save_travel_time(c->travel_secs);
        apply_travel_time(c);
        break;
    case APP_EVT_ZB_NET_LOST:
    case APP_EVT_ZB_NET_JOINED:
        ctl_refresh_outputs(c);
        break;
    case APP_EVT_MOTION_DONE:
        on_motion_done(c, ev->steps, ev->completed);
        break;
    case APP_EVT_CAL_TIMEOUT:
        if (!in_cal(c)) break;
        if (moving(c)) {
            /* finish stopping; abort on DONE so the DONE steps aren't written
             * into position as trusted state */
            c->cal_abort_pending = true;
            c->io->motion_stop();
        } else {
            cal_abort(c);
            ctl_refresh_outputs(c);
        }
        break;
    case APP_EVT_REPORT_TICK:
        if (moving(c) && ctl_calibrated(c)) {
            position_t tmp = c->pos;
            position_set_current(&tmp, c->io->motion_steps());
            c->io->report_lift(position_lift_pct(&tmp));
        }
        break;
    case APP_EVT_IDENTIFY:
        c->identifying = ev->on;
        ctl_refresh_outputs(c);
        break;
    default:
        break;
    }
    deadman(c);
}

void ctl_init(ctl_t *c, const ctl_ports_t *io, const ctl_config_t *cfg,
              const blind_store_data_t *boot)
{
    *c = (ctl_t){0};
    c->io  = io;
    c->cfg = *cfg;
    position_init(&c->pos, boot->span_valid, boot->closed_steps,
                  boot->pos_known, boot->cur_steps);
    if (boot->move_in_progress) {
        /* power died mid-move: position no longer trusted (spec §6) */
        position_mark_unknown(&c->pos);
        io->save_position(false, 0);
        io->save_move_flag(false);
        io->log("unclean shutdown mid-move -> Position Unknown, re-home needed");
    }
    c->reversed    = boot->motor_reversed;
    c->travel_secs = boot->travel_secs;
    /* Cruise speed must be correct before the dispatcher can serve a keypad
     * tap, which is well before the Zigbee join completes. */
    recompute_cruise(c);
    c->raw = c->pos.pos_known ? c->pos.cur_steps : 0;
}
