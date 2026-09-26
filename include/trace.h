#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>
#include "trace_ring.h"

/* Ships ON. A trace you must enable is a trace that is absent when the fault
 * happens: enabling means reflashing, reflashing resets the board, and a
 * reset clears exactly the class of fault this exists to catch. The flag is
 * here to strip the facility if RAM or flash gets tight, not as a mode. */
#ifndef TRACE_ENABLED
#define TRACE_ENABLED 1
#endif

typedef enum {
    TRC_BOOT = 1,      /* a = esp_reset_reason()   b = -                    */
    TRC_KEY_EVENT,     /* a = kp_event_type_t      b = key_id_t | 0x100 if
                                                     position_calibrated()   */
    TRC_MOVE_START,    /* a = from_steps           b = to_steps             */
    TRC_MOVE_NOOP,     /* a = target                b = closed_steps        */
    TRC_MOVE_REFUSED,  /* a = esp_err_t            b = hard_cap             */
    TRC_MOVE_DONE,     /* a = steps                b = completed            */
    TRC_ZB_CMD,        /* a = pct                  b = 1 if parked pending  */
    TRC_QUEUE_FULL,    /* a = dropped event type   b = -                    */

    /* Keypad telemetry. New codes go at the END: the ring survives resets, so
     * renumbering would make a dump written by the previous firmware decode as
     * something else entirely.
     *
     * These two cover the blind spot in everything above — every other code
     * records what the dispatcher DECIDED, and so cannot see a press that
     * never became an event, nor one that arrived correctly but late. */
    TRC_KEY_SWALLOWED, /* a = key_id_t             b = deepest excursion
                                                     | (count << 16), coalesced
                                                     over one second          */
    TRC_KEY_EDGE,      /* a = key_id_t | 0x100 if  b = samples the edge took,
                            this was the make        against FILTER_SAMPLES
                            edge                     for a clean one          */

    /* The classifier's suppression latches, emitted on every change. A gesture
     * that keypad_logic suppresses returns KP_EVT_NONE and so reaches nothing
     * downstream — invisible to KEY_EVENT and to the filter telemetry alike.
     * The state worth hunting is b=2 (in_reset) or b=1 (in_chord) persisting
     * with a=0: a latch held with no key down, which is unrecoverable without
     * a reboot and kills every key.
     *   a = bit0/1/2  UP/DOWN/FN down, bit4/5 UP/DOWN holding
     *   b = bit2 long_fired, bit3 reset_fired.
     *       Bits 0 and 1 are RESERVED — they carried in_chord and in_reset,
     *       the suppression latches that could outlive their gesture and leave
     *       the keypad dead until a power cycle. Both are removed. In a dump
     *       written by older firmware, b=1 or b=3 with a=0 IS that fault.     */
    TRC_KEY_LATCH,

    /* Proof the 5 ms poller is still running, and what it sees. Without it,
     * "poller stopped", "line never moved" and "gesture suppressed" are the
     * same silence in a dump.
     *   a = polls since the last heartbeat (12000 at a healthy 5 ms/60 s)
     *   b = bit0/1/2 raw GPIO level, bit4/5/6 debounced level, per key       */
    TRC_KEY_ALIVE,

    /* The dispatcher stopped a jog because its key reads released while no
     * HOLD_END arrived (dropped on a full queue).
     *   a = key_id_t   b = -                                                  */
    TRC_DEADMAN_STOP,

    /* The motion ISR could not post MOTION_DONE. Cannot happen while the
     * dispatcher keeps at most one move outstanding (the done queue has one
     * slot); recorded so a broken invariant is visible, not silent.
     *   a = failures so far   b = -                                         */
    TRC_DONE_POST_FAILED,
} trace_code_t;

/* Validate the RTC ring; clear it only if this was a cold boot. Emits nothing
 * so the caller can dump the previous session before marking the new one. */
void trace_init(void);

/* Print the ring, oldest first. Boot-time only, called before any other
 * emitter exists, so it takes no lock — by design, not oversight. Holding
 * the spinlock across ~257 ESP_LOGI calls would mask the step ISR for
 * hundreds of milliseconds; do not add one. */
void trace_dump(void);

/* Safe from task context, esp_timer callbacks and the Zigbee stack callback. */
void trace_emit(uint16_t code, int32_t a, int32_t b);

#if TRACE_ENABLED
#  define TRACE(c, a, b) trace_emit((uint16_t)(c), (int32_t)(a), (int32_t)(b))
#else
   /* Discarding (a)/(b) entirely would let a future side-effecting argument
    * silently change behaviour between TRACE_ENABLED builds. sizeof() keeps
    * the expression type-checked without evaluating it. */
#  define TRACE(c, a, b) ((void)sizeof((c) + (a) + (b)))
#endif

#endif /* TRACE_H */
