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
