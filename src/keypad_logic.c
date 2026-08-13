/**
 * @file keypad_logic.c
 * @brief Pure gesture classifier. Semantics (CONTEXT.md):
 *        - Tap: press+release < hold_ms.
 *        - Hold (jog): Up/Down held >= hold_ms -> HOLD_START, release -> HOLD_END.
 *        - Fn long-press >= long_ms -> FN_LONG (Fn never jogs). Suppressed
 *          while Up or Down is down.
 *        - All three held >= reset_ms -> FACTORY_RESET.
 *
 *        Every key is classified independently. See kp_state_t's comment for
 *        why: the suppression latches this file used to carry could outlive
 *        the gesture that set them and take the whole keypad with them.
 */
#include "keypad_logic.h"

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms, uint32_t reset_ms)
{
    *s = (kp_state_t){ .hold_ms = hold_ms, .long_ms = long_ms,
                       .reset_ms = reset_ms };
}

static kp_event_t evt(kp_event_type_t t, key_id_t k)
{
    return (kp_event_t){ .type = t, .key = k };
}

static bool all_three_down(const kp_state_t *s)
{
    return s->down[KEY_UP] && s->down[KEY_DOWN] && s->down[KEY_FN];
}

kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms)
{
    if (key >= KEY_COUNT) return evt(KP_EVT_NONE, key);

    if (pressed) {
        s->down[key]    = true;
        s->t_press[key] = now_ms;
        s->holding[key] = false;
        if (key == KEY_FN) s->long_fired = false;
        /* Arming is a timestamp, not a latch: nothing downstream consults it
         * unless all three are still down at the moment kp_on_tick looks. */
        if (all_three_down(s)) {
            s->reset_fired = false;
            s->t_reset     = now_ms;   /* timed from the press completing the trio */
        }
        return evt(KP_EVT_NONE, key);
    }

    /* Release. Deliberately unconditional on the other keys: a release must
     * never be swallowed because some other line happens to read low, which is
     * exactly how a stuck key used to disable the keys around it. A release is
     * also the only way a jog stops, so swallowing one leaves the motor
     * running. The cost is that abandoning a multi-key gesture emits a stray
     * tap per key; that is recoverable, and a dead keypad is not. */
    bool was_down = s->down[key];
    s->down[key] = false;
    if (!was_down) return evt(KP_EVT_NONE, key);

    if (key == KEY_FN && s->long_fired) {
        return evt(KP_EVT_NONE, key);   /* release after FN_LONG is silent */
    }
    if (s->holding[key]) {
        s->holding[key] = false;
        return evt(KP_EVT_HOLD_END, key);
    }
    if (key == KEY_FN) {
        /* Fn has no jog role: any release before long_ms is a tap (the
         * long-press case already returned above via long_fired). Without
         * this, presses between hold_ms and long_ms fall into a dead zone
         * and calibration marks silently vanish. */
        return evt(KP_EVT_TAP, key);
    }
    if (now_ms - s->t_press[key] < s->hold_ms) {
        return evt(KP_EVT_TAP, key);
    }
    /* held past hold_ms but HOLD_START never emitted (tick starvation):
     * treat as a completed hold with no motion — emit nothing. */
    return evt(KP_EVT_NONE, key);
}

kp_event_t kp_on_tick(kp_state_t *s, uint32_t now_ms)
{
    /* The three-key factory reset. Checked first so nothing can starve it.
     * Every term reads the current down[] state, so letting go of any key
     * abandons the gesture immediately and leaves nothing behind. */
    if (all_three_down(s)) {
        if (!s->reset_fired && now_ms - s->t_reset >= s->reset_ms) {
            s->reset_fired = true;
            return evt(KP_EVT_FACTORY_RESET, KEY_FN);
        }
        /* Don't also jog while the operator is holding all three down. This
         * is a guard on the instantaneous state, not a latch — it stops
         * applying the moment any key comes up. */
        return evt(KP_EVT_NONE, KEY_FN);
    }

    /* Fn long-press. The Up/Down guard keeps a three-key gesture from also
     * entering Calibration Mode on its way past long_ms. */
    if (s->down[KEY_FN] && !s->long_fired &&
        !s->down[KEY_UP] && !s->down[KEY_DOWN] &&
        now_ms - s->t_press[KEY_FN] >= s->long_ms) {
        s->long_fired = true;
        return evt(KP_EVT_FN_LONG, KEY_FN);
    }

    /* Up/Down hold -> jog (Fn never jogs) */
    for (key_id_t k = KEY_UP; k <= KEY_DOWN; k++) {
        if (s->down[k] && !s->holding[k] &&
            now_ms - s->t_press[k] >= s->hold_ms) {
            s->holding[k] = true;
            return evt(KP_EVT_HOLD_START, k);
        }
    }
    return evt(KP_EVT_NONE, KEY_UP);
}
