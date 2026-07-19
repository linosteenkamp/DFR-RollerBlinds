/**
 * @file keypad_logic.c
 * @brief Pure gesture classifier. Semantics (CONTEXT.md):
 *        - Tap: press+release < hold_ms.
 *        - Hold (jog): Up/Down held >= hold_ms -> HOLD_START, release -> HOLD_END.
 *        - Fn long-press >= long_ms -> FN_LONG (Fn never jogs).
 *        - Up+Down both held >= long_ms (from the second press) -> CHORD_REVERSE.
 *          A chord attempt (both down together) suppresses the individual keys'
 *          TAP/HOLD events entirely, fired or not — fat-finger safety.
 */
#include "keypad_logic.h"

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms)
{
    *s = (kp_state_t){ .hold_ms = hold_ms, .long_ms = long_ms };
}

static kp_event_t evt(kp_event_type_t t, key_id_t k)
{
    return (kp_event_t){ .type = t, .key = k };
}

kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms)
{
    if (key >= KEY_COUNT) return evt(KP_EVT_NONE, key);

    if (pressed) {
        s->down[key]    = true;
        s->t_press[key] = now_ms;
        s->holding[key] = false;
        if (key == KEY_FN) s->long_fired = false;
        if ((key == KEY_UP || key == KEY_DOWN) &&
            s->down[KEY_UP] && s->down[KEY_DOWN] && !s->in_chord) {
            s->in_chord    = true;   /* latch: suppress both keys until released */
            s->chord_fired = false;
            /* chord timing restarts from this (second) press */
            s->t_press[KEY_UP] = s->t_press[KEY_DOWN] = now_ms;
            /* if the other key was mid-jog, close that hold before suppressing
             * it — otherwise the consumer never gets the jog-stop signal */
            key_id_t other = (key == KEY_UP) ? KEY_DOWN : KEY_UP;
            if (s->holding[other]) {
                s->holding[other] = false;
                return evt(KP_EVT_HOLD_END, other);
            }
        }
        return evt(KP_EVT_NONE, key);
    }

    /* release */
    bool was_down = s->down[key];
    s->down[key] = false;
    if (!was_down) return evt(KP_EVT_NONE, key);

    if (s->in_chord && (key == KEY_UP || key == KEY_DOWN)) {
        if (!s->down[KEY_UP] && !s->down[KEY_DOWN]) {
            s->in_chord = false;     /* both released: chord attempt over */
        }
        return evt(KP_EVT_NONE, key);   /* chord suppresses individual events */
    }
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
    /* Fn is independent of the Up/Down chord — checked first so a chord
     * attempt can never starve FN_LONG */
    if (s->down[KEY_FN] && !s->long_fired &&
        now_ms - s->t_press[KEY_FN] >= s->long_ms) {
        s->long_fired = true;
        return evt(KP_EVT_FN_LONG, KEY_FN);
    }

    /* chord: suppresses Up/Down hold processing */
    if (s->in_chord && !s->chord_fired &&
        s->down[KEY_UP] && s->down[KEY_DOWN] &&
        now_ms - s->t_press[KEY_UP] >= s->long_ms) {
        s->chord_fired = true;
        return evt(KP_EVT_CHORD_REVERSE, KEY_UP);
    }
    if (s->in_chord) return evt(KP_EVT_NONE, KEY_UP);

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
