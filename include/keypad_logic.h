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
    KP_EVT_CHORD_REVERSE, /* RETIRED — never emitted. The value is retained so a
                           * trace dump written by older firmware still decodes
                           * correctly: the RTC ring survives a reset, and
                           * TRC_KEY_EVENT stores this enum in its `a` field, so
                           * renumbering would silently relabel old records. */
    KP_EVT_FACTORY_RESET, /* all three keys held past reset_ms (Zigbee reset) */
} kp_event_type_t;

typedef struct {
    kp_event_type_t type;
    key_id_t        key;   /* meaningful for TAP / HOLD_START / HOLD_END */
} kp_event_t;

/* No key's state may depend on another key's state persisting.
 *
 * This classifier used to carry two suppression latches — in_chord for the
 * Up+Down reverse gesture, in_reset for the three-key factory reset — each of
 * which cleared only when a specific set of keys was simultaneously released.
 * A line that reads low, for any reason, means that moment never arrives and
 * the latch outlives the gesture forever: the keypad is dead until reboot.
 * in_reset suppressed all three keys, so a single failing line could take the
 * whole keypad with it. That was observed in the field.
 *
 * Both are gone. What remains is per-key state plus timestamps. The only
 * cross-key conditions left read the CURRENT down[] and nothing else, so they
 * self-clear the instant a key is released and cannot strand anything.
 *
 * If you add cross-key state here, that property is what you are spending. */
typedef struct {
    uint32_t hold_ms;
    uint32_t long_ms;
    uint32_t reset_ms;              /* all-three-keys hold for factory reset */
    bool     down[KEY_COUNT];       /* current pressed state */
    uint32_t t_press[KEY_COUNT];    /* press timestamp */
    bool     holding[KEY_COUNT];    /* HOLD_START already emitted */
    bool     long_fired;            /* FN_LONG emitted for this Fn press */
    bool     reset_fired;           /* RESET emitted for this trio */
    uint32_t t_reset;               /* when the trio was completed */
} kp_state_t;

void kp_init(kp_state_t *s, uint32_t hold_ms, uint32_t long_ms, uint32_t reset_ms);

/* Feed a debounced edge. Returns at most one event (KP_EVT_NONE otherwise). */
kp_event_t kp_on_change(kp_state_t *s, key_id_t key, bool pressed, uint32_t now_ms);

/* Call periodically (~5 ms). Emits time-based events: HOLD_START, FN_LONG,
 * FACTORY_RESET. Returns at most one event per call. */
kp_event_t kp_on_tick(kp_state_t *s, uint32_t now_ms);

#endif /* KEYPAD_LOGIC_H */
