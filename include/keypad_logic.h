#ifndef KEYPAD_LOGIC_H
#define KEYPAD_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_UP = 0, KEY_DOWN, KEY_FN, KEY_COUNT } key_id_t;

typedef enum {
    KP_EVT_NONE = 0,
    KP_EVT_TAP,           /* press+release shorter than hold_ms */
    KP_EVT_HOLD_START,    /* key held past hold_ms (jog begins) */
    KP_EVT_HOLD_END,      /* held key released (jog ends) */
    KP_EVT_FN_LONG,       /* Fn held past long_ms (calibration mode toggle) */
    KP_EVT_CHORD_REVERSE, /* Up+Down both held past long_ms (motor_reversed) */
} kp_event_type_t;

typedef struct {
    kp_event_type_t type;
    key_id_t        key;   /* meaningful for TAP / HOLD_START / HOLD_END */
} kp_event_t;

typedef struct {
    uint32_t hold_ms;
    uint32_t long_ms;
    bool     down[KEY_COUNT];       /* current pressed state */
    uint32_t t_press[KEY_COUNT];    /* press timestamp */
    bool     holding[KEY_COUNT];    /* HOLD_START already emitted */
    bool     long_fired;            /* FN_LONG emitted for this Fn press */
    bool     chord_fired;           /* CHORD emitted for this Up+Down press */
    bool     in_chord;              /* Up+Down suppression latch */
} kp_state_t;

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms);

/* Feed a debounced edge. Returns at most one event (KP_EVT_NONE otherwise). */
kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms);

/* Call periodically (~50 ms). Emits time-based events: HOLD_START, FN_LONG,
 * CHORD_REVERSE. Returns at most one event per call. */
kp_event_t kp_on_tick(kp_state_t *s, uint32_t now_ms);

#endif /* KEYPAD_LOGIC_H */
